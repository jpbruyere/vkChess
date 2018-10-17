/*
* Vulkan Chess PBR - Physical based rendering with glTF 2.0 model (metal/roughness workflow) with image based lighting
*
* Note: Requires the separate asset pack (see data/README.md)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
* Copyright (C) 2018 by jp_bruyere@hotmail.com (instanced rendering with texture array and material ubo)
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

// PBR reference: http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// glTF format: https://github.com/KhronosGroup/glTF
// tinyglTF loader: https://github.com/syoyo/tinygltf

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <vector>
#include <chrono>
#include <list>
#include <iostream>
#include <errno.h>

#include "VkEngine.h"
#include "VulkanSwapChain.hpp"
#include "vkrenderer.h"
#include "pbrrenderer2.h"

#include <glm/gtx/spline.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "vkvg.h"

#define CAPTURE_ZONE_HEIGHT 5

class VkChess : public vks::VkEngine
{
public:
    enum Color { White, Black };
    enum PceType {Pawn, Rook, Knight, Bishop, Queen, King };
    struct Piece
    {
        PceType     type;
        bool        promoted;
        Color       color;

        glm::ivec2  position;
        glm::ivec2  initPosition;
        float       yAngle;
        bool        captured;
        bool        hasMoved;

        uint32_t    instance;

        glm::vec3 getCurrentPosition(vkglTF::Model* mod) {
            glm::vec3 scale;
            glm::quat rotation;
            glm::vec3 translation;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(mod->instanceDatas[instance].modelMat, scale, rotation, translation, skew, perspective);
            return translation;
        }
    };
    struct animation{
        uint32_t uboIdx;
        std::queue<glm::mat4> queue;
    };

    //vks::vkRenderer*     debugRenderer = nullptr;
    pbrRenderer*   sceneRenderer = nullptr;

    VkvgDevice  vkvgDev;
    VkvgSurface surf = NULL;


    VkChess() : VkEngine(1024, 768, VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
        title = "Vulkan Chess glTf 2.0 PBR";
        camera.type = Camera::CameraType::firstperson;
        camera.movementSpeed = 8.0f;
        camera.rotationSpeed = 0.25f;
        camera.setPerspective(50.0f, (float)width / (float)height, 0.1f, 50.0f);
        camera.setRotation({ 42.0f, 0.0f, 0.0f });
        camera.setPosition({ .0f, -12.f, -15.f });

        settings.validation = true;

        phyInfos.enabledFeatures.samplerAnisotropy
                = phyInfos.features.samplerAnisotropy;
        phyInfos.selectQueue (phyInfos.pQueue);
    }

    ~VkChess()
    {
        vkDeviceWaitIdle        (device->dev);

        for (uint i=0; i<2; i++)
            for (uint j=0; j<6; j++)
                vkvg_surface_destroy(piecesImgs[i][j]);

        vkvg_surface_destroy    (surf);
        vkvg_device_destroy     (vkvgDev);

        //delete(debugRenderer);
        delete(sceneRenderer);
    }

    const glm::vec4 hoverColor      = glm::vec4(0.0,0.0,0.1,1.0);
    const glm::vec4 selectedColor   = glm::vec4(0.0,0.0,0.2,1.0);
    const glm::vec4 validMoveColor  = glm::vec4(0.0,0.0,0.4,1.0);
    const glm::vec4 bestMoveColor   = glm::vec4(0.0,0.2,0.0,1.0);
    const glm::vec4 checkColor      = glm::vec4(0.6,0.0,0.0,1.0);

    Color currentPlayer = White;
    bool gameStarted    = false;
    bool playerIsAi[2]  = {false,true};
    bool playerWin[2]   = {false,false};
    Piece pieces[32];
    Piece* board[8][8]  = {};

    int cptWhiteOut = 0;
    int cptBlackOut = 0;

    int animSteps = 150;
    std::vector <animation> animations;

    glm::ivec2 bestMoveOrig     = glm::ivec2(-1,-1);
    glm::ivec2 bestMoveTarget   = glm::ivec2(-1,-1);

    glm::ivec2 hoverSquare      = glm::ivec2(-1,-1);
    glm::ivec2 selectedSquare   = glm::ivec2(-1,-1);

    //stockfish
    bool stockFishIsReady   = false;
    bool hint               = false;
    uint8_t level[2]        = {20,0};
    char* nextCommand;
    int sfPid, sfReadfd, sfWritefd;

    char sfOutBuff[1024];
    int sfOutBuffPtr        = 0;

    std::string sfcmdGo = "go movetime 50\n";
    const char* initPosCmd = "position startpos moves ";
    char movesBuffer[40000];
    int previouMovesPtr;
    int movesPtr;

    float angle = 0.f;

    void svg_set_color (VkvgContext ctx, uint32_t c, float alpha) {
        float a = (c >> 24 & 255) / 255.f;
        float b = (c >> 16 & 255) / 255.f;
        float g = (c >> 8 & 255) / 255.f;
        float r = (c & 255) / 255.f;
        vkvg_set_source_rgba(ctx,r,g,b,a*alpha);
    }

    void vkvg_test() {
        const int x = 300;
        const int y = 100;
        VkvgContext ctx = vkvg_create(surf);

        //vkvg_set_source_surface(ctx, piecesImgs[0][0], x, y);
        for (int i=0; i<32; i++){
            vkvg_set_source_surface(ctx, piecesImgs[0][0], x+i*50, y);
            vkvg_paint(ctx);
        }


        vkvg_destroy (ctx);
    }

    void print_winner () {
        const int x = 200;
        const int y = 250;
        VkvgContext ctx = vkvg_create(surf);

        std::string msg;
        if (playerWin[White])
            msg = "Whites win";
        else if (playerWin[Black])
            msg = "Blacks win";
        else
            msg = "Pad";

        vkvg_move_to(ctx,x+5,y+5);
        vkvg_set_font_size(ctx,120);
        vkvg_select_font_face(ctx,"mono");
        vkvg_set_source_rgba (ctx, 0,0,0,1);
        vkvg_show_text(ctx, msg.c_str());
        //vkvg_flush(ctx);
        vkvg_move_to(ctx,x,y);
        vkvg_set_source_rgba (ctx, 1,1,1,1);
        vkvg_show_text(ctx, msg.c_str());

        vkvg_destroy (ctx);
    }
    void vkvg_print_fps() {
        const int x = 10;
        const int y = 64;
        VkvgContext ctx = vkvg_create(surf);
        vkvg_set_operator(ctx,VKVG_OPERATOR_CLEAR);
        vkvg_rectangle(ctx,x,y-50,160,60);
        vkvg_fill(ctx);
        vkvg_set_operator(ctx,VKVG_OPERATOR_OVER);
        //vkvg_set_source_rgba (ctx, 0.6,0.6,1.0,0.5);
        //vkvg_fill(ctx);
        vkvg_move_to(ctx,x+5,y+5);
        vkvg_set_font_size(ctx,64);
        vkvg_select_font_face(ctx,"mono");
        vkvg_set_source_rgba (ctx, 0,0,0,1);
        vkvg_show_text(ctx, std::to_string(lastFPS).c_str());
        //vkvg_flush(ctx);
        vkvg_move_to(ctx,x,y);
        vkvg_set_source_rgba (ctx, 1,1,1,1);
        vkvg_show_text(ctx, std::to_string(lastFPS).c_str());

        vkvg_destroy (ctx);
    }
    void updateMiniBoard() {
        //vkvg_surface_clear (surf);

        /*vkvg_matrix_t mat;
        vkvg_matrix_init_translate (&mat, 105,105);
        vkvg_matrix_rotate(&mat,angle);
        vkvg_matrix_translate(&mat,-105,-105);*/


        VkvgContext ctx = vkvg_create(surf);
        vkvg_clear(ctx);

        vkvg_scale(ctx, 0.5,0.5);

        //vkvg_set_matrix(ctx,&mat);
        const int x = 100;
        const int y = 150;
        const int caseSize = 40;
        const int margin = 5;

        vkvg_set_source_rgba (ctx, 0,0,0,1);

        vkvg_rectangle(ctx, x,y,8*caseSize+2*margin,8*caseSize+2*margin);
        vkvg_stroke_preserve(ctx);
        vkvg_set_source_rgba (ctx, 0.8,0.8,0.8,0.3);
        vkvg_fill(ctx);

        vkvg_rectangle(ctx, x+margin,y+margin,8*caseSize,8*caseSize);
        vkvg_set_source_rgba (ctx, 0.0,0.0,0.0,0.5);
        vkvg_fill(ctx);

        //vkvg_set_source_rgba (ctx, 0,0,0,1);
        //vkvg_move_to(ctx,20,20);
        vkvg_set_source_rgba (ctx, 0.8,0.8,0.8,.5);
        for(int cx=0; cx<4; cx++) {
            for(int cy=0; cy<8; cy++) {
                vkvg_rectangle(ctx, x + margin + cx * caseSize * 2 + cy%2 * caseSize,
                               y + margin + cy * caseSize, caseSize, caseSize);
            }
        }
        vkvg_fill(ctx);

        vkvg_set_source_rgba (ctx, 1,0,0,1);
        //vkvg_scale(ctx,0.5,0.5);
        for(uint i=0; i<32; i++) {
            int cx = pieces[i].position.x * caseSize + x + margin + caseSize/2 - vkvg_surface_get_width(piecesImgs[pieces[i].color][pieces[i].type])/2;// + caseSize / 2;
            int cy = y + margin + 7*caseSize - pieces[i].position.y * caseSize +2;// + caseSize / 2;
            vkvg_set_source_surface(ctx, piecesImgs[pieces[i].color][pieces[i].type], cx*0.5, cy*0.5);
            vkvg_paint(ctx);
        }


//        vkvg_select_font_face (ctx, "mono");
//        vkvg_set_font_size (ctx, 20.0);
//        vkvg_show_text(ctx, "this is a test");

        vkvg_destroy (ctx);
        angle+=0.0005f;
    }
    void update(){
        readStockfishLine();

        std::vector<animation>::iterator itr = animations.begin();
        for ( ; itr != animations.end(); ) {
            if (itr->queue.empty()) {
                itr = animations.erase(itr);
            } else {
                glm::mat4 a = itr->queue.front();
                itr->queue.pop();
                mod->instanceDatas[itr->uboIdx].modelMat = a;
                mod->setInstanceIsDirty(itr->uboIdx);
                ++itr;
            }
        }

        mod->updateInstancesBuffer();

        //updateMiniBoard();
        vkvg_print_fps();

        //vkvg_test();
    }

    Piece* getPiece (glm::ivec2 pos){
        return board[pos.x][pos.y];
    }

    void animatePce (uint32_t pceIdx, float x, float y) {
        glm::vec3 vUp = glm::vec3(0.f,-10.f,0.f);
        glm::vec3 start = glm::vec3(mod->instanceDatas[pceIdx].modelMat[3]);
        glm::vec3 end   = glm::vec3(x*2 - 7, 0.f, 7 - y*2);

        animation a;
        a.uboIdx = pceIdx;
        animSteps = std::max(lastFPS/2, 60u);

        for (int i = 0; i<=animSteps; i++){
            float t = (float)i / (float)animSteps;
            a.queue.push (glm::translate (glm::mat4(1.0), glm::catmullRom (start+vUp, start, end, end+vUp, t)));
        }
        animations.push_back(a);
    }
    void toogleHint() {
        hint = !hint;

        clearBestMove();

        if (playerIsAi[currentPlayer])
            return;
        if (hint) {
            sendPositionsCmd();
            write(sfWritefd,"go infinite\n", 12);
        }else{
            write(sfWritefd,"stop\n",5);
        }
    }

    void sendPositionsCmd (){
        if (movesPtr==0)
            return;
        char levelBuf[100];
        std::stringstream out;
        out << "setoption name Skill Level value " << std::to_string(level[currentPlayer]) << std::endl;
        out << std::string(movesBuffer, movesPtr-1) << std::endl;
        //out << sfcmdGo;
        //sprintf (levelBuf,"setoption name Skill Level value %d\n",level[currentPlayer]);
        //write(sfWritefd, levelBuf, strlen(levelBuf));
        //write(sfWritefd, movesBuffer, movesPtr-1 );
        //write(sfWritefd, "\n", 1 );
        //write(sfWritefd, sfcmdGo.c_str() , sfcmdGo.length());

#if DEBUG_STOCKFISH
        std::cout << std::to_string(sfWritefd) << " <= " << out.str();
#endif

        write(sfWritefd, out.str().c_str(), out.tellp());

    }

    char* getBestMove() {
        getStockFishIsReady();
        sendPositionsCmd();
        write(sfWritefd, sfcmdGo.c_str() , sfcmdGo.length());
    }

    bool getStockFishIsReady () {
        write(sfWritefd,"isready\n",8);
        char buff[8];
        read(sfReadfd, buff, 8);
        if (strncmp(buff, "readyok\n", 8)==0)
            return true;
        else
            return false;
    }
    void getStockFishOutput () {
        int saved_flags = fcntl(sfReadfd, F_GETFL);
        char c;
        bool blockingRead = true;
        while (read (sfReadfd, &c, 1)==1){
            if (blockingRead){
                fcntl(sfReadfd, F_SETFL, O_NONBLOCK);//set pipe non blocking
                blockingRead = false;
            }
            sfOutBuff[sfOutBuffPtr++] = c;
        }
        sfOutBuff[sfOutBuffPtr] = 0;
        //std::cout << sfOutBuff << std::endl;
        sfOutBuffPtr = 0;
        fcntl(sfReadfd, F_SETFL, saved_flags & ~O_NONBLOCK);//set pipe blocking
    }

    int skipnspace(char* buff, int numSpaces){
        int ptr=0;
        for (int i=0; i<numSpaces; i++){
            while (buff[ptr++]!=0x20)
                continue;
        }
        return ptr;
    }
    void readStockfishLine () {
        char c = 0;
        char lineBuf[1024];
        int ptr = 0;
        if (read (sfReadfd, &c, 1)!=1)
            return;
        lineBuf[ptr++] = c;
        stockFishIsReady = false;
        while (c!='\n'){
            if (read (sfReadfd, &c, 1)==1)
                lineBuf[ptr++] = c;
        }
        lineBuf[ptr] = 0;

#if DEBUG_STOCKFISH
      std::cout << "=> " << lineBuf;
      std::flush(std::cout);
#endif
        if (strncmp (lineBuf, "readyok", 7)==0){
            stockFishIsReady = true;
            if (!gameStarted && playerIsAi[currentPlayer]) {
                gameStarted = true;
                startTurn();
            }
        }else if (strncmp (lineBuf, "uciok", 5)==0){

        }else if (strncmp (lineBuf, "info", 4)==0){
            if (playerIsAi[currentPlayer] || !hint)
                return;
            ptr=5;
            ptr+=skipnspace(lineBuf+ptr,2);
            if (strncmp(lineBuf+ptr, "seldepth", 8)!=0)
                return;
            ptr+=9;
            while (strncmp(lineBuf+ptr, " pv ", 4)!=0)
                ptr++;
            ptr+=4;

            if (bestMoveOrig.x >=0){
                subCaseLight(bestMoveOrig, bestMoveColor);
                subCaseLight(bestMoveTarget, bestMoveColor);
            }
            bestMoveOrig = glm::ivec2(lineBuf[ptr]-97, lineBuf[ptr+1]-49);
            bestMoveTarget = glm::ivec2(lineBuf[ptr+2]-97, lineBuf[ptr+3]-49);
            if (bestMoveOrig.x >=0){
                addCaseLight(bestMoveOrig, bestMoveColor);
                addCaseLight(bestMoveTarget, bestMoveColor);
            }

        }else if (strncmp (lineBuf, "bestmove", 8)==0){
            if (strncmp(lineBuf+9, "(none)", 6)==0) {
                gameStarted = false;
                if (currentPlayer == White)
                    playerWin[Black] = true;
                else
                    playerWin[White] = true;
                print_winner();
                return;
            }
            glm::ivec2 orig = glm::ivec2(lineBuf[9]-97, lineBuf[10]-49);
            glm::ivec2 dest = glm::ivec2(lineBuf[11]-97, lineBuf[12]-49);

            if (playerIsAi[currentPlayer]){
                PceType promotion = Pawn;
                switch (lineBuf[13]) {
                case 'q':
                    promotion = Queen;
                    break;
                case 'r':
                    promotion = Rook;
                    break;
                case 'b':
                    promotion = Bishop;
                    break;
                case 'n':
                    promotion = Knight;
                    break;
                }
                processMove (orig, dest, promotion);
                switchPlayer();
            }else if (hint){
                switchPlayer();
            }
        }else if (strncmp (lineBuf, "option", 6)==0){

        }
    }
    void startStockFish () {
        int pipeA[2],pipeB[2];

        pipe(pipeA);
        pipe(pipeB);

        int stockFishPid;
        stockFishPid = fork();

        if (stockFishPid>0)
        {
            //parent
            sfReadfd = pipeA[0];
            sfWritefd = pipeB[1];
            close(pipeA[1]);
            close(pipeB[0]);

            fcntl(sfReadfd, F_SETFL, O_NONBLOCK);//set read non blocking

            //get onStart preemble with credits
            char c;
            while (true){
                if (read (sfReadfd, &c, 1)!=1)
                    continue;
                if (c=='\n')
                    break;
                std::cout << c;
            }
            std::cout << std::endl;

            write(sfWritefd,"uci\n",4);

        } else if (stockFishPid==0) {
            // child
            int rfd = pipeA[1];
            int wfd = pipeB[0];
            close(pipeA[0]);
            close(pipeB[1]);

            dup2(wfd, STDIN_FILENO);
            dup2(rfd, STDOUT_FILENO);  // send stdout to the pipe
            dup2(wfd, STDERR_FILENO);  // send stderr to the pipe

            close(rfd);
            close(wfd);

            std::system("stockfish");

            perror ("StockFish terminated.\n");
            _exit(0);
        }
    }

    //actualize board[][] array
    void boardMove (Piece* pce, glm::ivec2 newPos, bool animate = false) {
        if (!pce->captured)
            board[pce->position.x][pce->position.y] = nullptr;
        board[newPos.x][newPos.y] = pce;
        pce->position = newPos;
        pce->hasMoved = true;
        if (animate) {
            animatePce (pce->instance, (float)newPos.x,(float)newPos.y);
            updateMiniBoard();
        }
    }
    void capturePce (Piece* p, bool animate = false) {
        board[p->position.x][p->position.y] = nullptr;
        p->captured = true;
        p->hasMoved = true;

        if (p->color == White){
            p->position = glm::ivec2(-2 - cptWhiteOut / CAPTURE_ZONE_HEIGHT, 7 - cptWhiteOut % CAPTURE_ZONE_HEIGHT);
            cptWhiteOut ++;
        }else{
            p->position = glm::ivec2(9 + cptBlackOut / CAPTURE_ZONE_HEIGHT, 7 - cptBlackOut % CAPTURE_ZONE_HEIGHT);
            cptBlackOut ++;
        }

        if (!animate)
            return;

        if (p->promoted)
            resetPromotion(p,true);
        animatePce (p->instance, (float)p->position.x,(float)p->position.y);
    }

    void resetPromotion (Piece* p, bool rebuildCmdBuffs = true) {
        p->type = Pawn;
        p->promoted = false;
        mod->instances[p->instance] = mod->getPrimitiveIndex("pawn");
        if (rebuildCmdBuffs)
            rebuildCommandBuffers();
    }
    void promote(Piece* p, PceType promotion) {
        uint32_t primIdx;
        switch (promotion) {
        case Queen:
            primIdx = mod->getPrimitiveIndex("queen");
            break;
        case Bishop:
            primIdx = mod->getPrimitiveIndex("bishop");
            break;
        case Rook:
            primIdx = mod->getPrimitiveIndex("rook");
            break;
        case Knight:
            primIdx = mod->getPrimitiveIndex("knight");
            break;
        }
        mod->instances[p->instance] = primIdx;
        rebuildCommandBuffers();
        p->promoted = true;
        p->type = promotion;
    }

    void replay (int untilPtr) {
        resetBoard(false);

        while (movesPtr<untilPtr) {
            glm::ivec2 orig = glm::ivec2(movesBuffer[movesPtr]-97, movesBuffer[movesPtr+1]-49);
            glm::ivec2 dest = glm::ivec2(movesBuffer[movesPtr+2]-97, movesBuffer[movesPtr+3]-49);
            std::cout << "replay (" << std::to_string(orig.x + 1) << ", " << std::to_string(orig.y + 1) << ") => ("
                      << std::to_string(dest.x + 1) << ", " << std::to_string(dest.y + 1) << ")" << std::endl;
            PceType promotion = Pawn;
            if (movesBuffer[movesPtr+4] != 0x20) {
                switch (movesBuffer[movesPtr+4]) {
                case 'q':
                    promotion = Queen;
                    break;
                case 'r':
                    promotion = Rook;
                    break;
                case 'b':
                    promotion = Bishop;
                    break;
                case 'n':
                    promotion = Knight;
                    break;
                }
            }
            processMove (orig, dest, promotion, false);
            switchPlayer(false);
        }
    }
    void undo () {

        if (movesPtr < 25)
            return;
        std::cout << "Previous  : " + std::string(movesBuffer,movesPtr) << std::endl;
        replay (previouMovesPtr);
        if (playerIsAi[currentPlayer] && movesPtr>24)
            replay (previouMovesPtr);
        std::cout << "After undo: " + std::string(movesBuffer,movesPtr) << std::endl;
        rebuildCommandBuffers();
        for (int i=0; i<32; i++){
            glm::vec3 curPos = pieces[i].getCurrentPosition(mod);
            //glm::vec3(x*2 - 7,0, 7 - y*2)),
            if ((int)curPos.x != (pieces[i].position.x*2-7) || (int)curPos.z != (7-2*pieces[i].position.y))
                animatePce (pieces[i].instance, (float)pieces[i].position.x,(float)pieces[i].position.y);
        }
        updateMiniBoard();
        startTurn();
    }

    void processMove (glm::ivec2 orig, glm::ivec2 dest, PceType promotion = Pawn, bool animate = true) {
        if (orig == dest)
            return;
        Piece* p    = getPiece(orig);
        Piece* pDest= getPiece(dest);


        if (p) {
            if (p->type == King && animate)
                setCaseRedLight(p->position, 0);

            if (p->type == King && abs(orig.x - dest.x)>1){//rocking
                //move tower
                if (dest.x == 6)//right tower
                    boardMove(board[7][dest.y], glm::ivec2(5,dest.y), animate);
                else
                    boardMove(board[0][dest.y], glm::ivec2(3,dest.y), animate);
            }else if (pDest) {//capture
                if (pDest->color != p->color)
                    capturePce (pDest, animate);
                else {
                    std::cerr << "Unexpected Piece on case: (" << dest.x << "," << dest.y << ")" << std::endl;
                    exit(-1);
                }
            }else if (p->type == Pawn) {
                if (orig.x != dest.x) //pawn attack
                    capturePce (board[dest.x][orig.y], animate);
            }
            //normal move
            boardMove(board[orig.x][orig.y], dest, animate);
        }

        previouMovesPtr = movesPtr;
        movesBuffer[movesPtr++]=orig.x + 97;//ascii pos
        movesBuffer[movesPtr++]=orig.y + 49;
        movesBuffer[movesPtr++]=dest.x + 97;
        movesBuffer[movesPtr++]=dest.y + 49;

        if (promotion != Pawn) {
            switch (promotion) {
            case Queen:
                movesBuffer[movesPtr++]='q';
                break;
            case Bishop:
                movesBuffer[movesPtr++]='b';
                break;
            case Rook:
                movesBuffer[movesPtr++]='r';
                break;
            case Knight:
                movesBuffer[movesPtr++]='n';
                break;
            }
            promote(p, promotion);
        }

        movesBuffer[movesPtr++]=0x20;//space
    }

    bool validateMoves (Piece* p) {
        std::vector<glm::ivec2> moves = validMoves;

        std::vector<glm::ivec2>::iterator itr = moves.begin();
        for ( ; itr != moves.end(); ) {
            if (!PreviewBoard(p, *itr))
                itr = moves.erase(itr);
             else
                ++itr;
        }
        validMoves = moves;
    }
    Piece* getKing (Color player) {
        return (player==White)? &pieces[4] : &pieces[20];
    }
    bool kingIsSafe(Color player){
        Piece* k = getKing(player);

        int pStartOffset = (k->color == White)?16:0;//we check moves of opponent
        for (int pIdx=pStartOffset; pIdx<pStartOffset+16; pIdx++) {
            validMoves.clear();
            computeValidMove(&pieces[pIdx]);
            if (std::find(validMoves.begin(), validMoves.end(), k->position)!=validMoves.end()){
                validMoves.clear();
                return false;
            }
        }
        validMoves.clear();
        return true;
    }

    bool PreviewBoard(Piece* p, glm::ivec2 newPos){
        std::vector<glm::ivec2> saveCurrentValidMoves = validMoves;
        Piece savedPces[32] = {};
        memcpy (savedPces, pieces, 32*sizeof(Piece));
        Piece* savedBoard[8][8] = {};
        memcpy (savedBoard, board, 64*sizeof(Piece*));
        int savedCptWhiteOut = cptWhiteOut;
        int savedCptBlackOut = cptBlackOut;

        processMove (p->position, newPos, Pawn, false);

        bool kingOk = kingIsSafe(p->color);

        previouMovesPtr-=5;
        movesPtr-=5;
        memcpy (pieces, savedPces, 32*sizeof(Piece));
        memcpy (board, savedBoard, 64*sizeof(Piece*));
        validMoves = saveCurrentValidMoves;
        cptWhiteOut = savedCptWhiteOut;
        cptBlackOut = savedCptBlackOut;

        return kingOk;
    }

    std::vector<glm::ivec2> validMoves;

    void checkSingleMove (Piece* p, int deltaX, int deltaY) {
        glm::ivec2 delta = glm::ivec2(deltaX, deltaY);
        glm::ivec2 newPos = p->position + delta;

        if (newPos.x < 0 || newPos.x > 7 || newPos.y < 0 || newPos.y > 7)
            return;

        Piece* target = board[newPos.x][newPos.y];

        if (!target) {//target cell is empty
            if (p->type == Pawn){//current cell is pawn
                if (delta.x != 0){//check En passant capturing
                    if (previouMovesPtr<24)
                        return;
                    target = board[newPos.x][p->position.y];
                    if (!target)
                        return;
                    if (target->color == p->color || p->type != Pawn)
                        return;
                    if (p->color == Black) {
                        if (newPos.y != 2)
                            return;
                        if ((movesBuffer[previouMovesPtr]-97 != newPos.x) ||
                                (movesBuffer[previouMovesPtr+2]-97 != newPos.x) ||//not a straight move asside
                                (movesBuffer[previouMovesPtr+1]-49 != 1) ||
                                (movesBuffer[previouMovesPtr+3]-49 != 3))
                            return;
                    }else{
                        if (newPos.y != 5)
                            return;
                        if ((movesBuffer[previouMovesPtr]-97 != newPos.x) ||
                                (movesBuffer[previouMovesPtr+2]-97 != newPos.x) ||//not a straight move asside
                                (movesBuffer[previouMovesPtr+1]-49 != 6) ||
                                (movesBuffer[previouMovesPtr+3]-49 != 4))
                            return;
                    }
                }
            }
            validMoves.push_back(newPos);
            return;
        }else if (p->color == target->color)//target cell is not empty
            return;
        else if (p->type == Pawn && deltaX == 0)//pawn cant take forward
            return;

        validMoves.push_back(newPos);
    }
    void checkIncrementalMove (Piece* p, int deltaX, int deltaY){
        glm::ivec2 delta = glm::ivec2(deltaX, deltaY);
        glm::ivec2 newPos = p->position + delta;

        while (newPos.x >= 0 && newPos.x < 8 && newPos.y >= 0 && newPos.y < 8){
            Piece* target = board[newPos.x][newPos.y];
            if (target) {
                if (target->color != p->color)
                    validMoves.push_back(newPos);
                break;
            }
            validMoves.push_back(newPos);
            newPos += delta;
        }
    }

    void checkCastling (Piece* k) {
        if (k->hasMoved)
            return;
        bool castlingOk = true;
        int pceOffset = (k->color == Black) ? 16 : 0;
        //TODO: check king is safe on rook position

        //check if tower is in place
        if (!pieces[pceOffset].hasMoved) {
            //castling long
            for (int i=k->position.x-1; i>0; i--){
                if (board[i][k->position.y]){
                    castlingOk=false;
                    break;
                }
                if (!PreviewBoard(k, glm::ivec2(i, k->position.y))){
                    castlingOk=false;
                    break;
                }
            }
            if (castlingOk && !pieces[pceOffset].hasMoved)
                validMoves.push_back(glm::ivec2(k->position.x-2,k->position.y));
        }
        //castling short
        if (!pieces[pceOffset + 7].hasMoved) {
            castlingOk = true;
            for (int i=k->position.x+1; i<7;i++){
                if (board[i][k->position.y]){
                    castlingOk=false;
                    break;
                }
                if (!PreviewBoard(k, glm::ivec2(i, k->position.y))){
                    castlingOk=false;
                    break;
                }
            }
            if (castlingOk && !pieces[7+pceOffset].hasMoved)
                validMoves.push_back(glm::ivec2(k->position.x+2,k->position.y));
        }
    }
    void computeValidMove(Piece* p){
        if (p->captured)
            return;

        int validMoveStart = validMoves.size();

        if (p->type == Pawn) {
            int pawnDirection = (p->color == White)? 1 : -1;
            checkSingleMove (p, 0, 1 * pawnDirection);
            if (!p->hasMoved)
                checkSingleMove (p, 0, 2 * pawnDirection);
            checkSingleMove (p,-1, 1 * pawnDirection);
            checkSingleMove (p, 1, 1 * pawnDirection);
        }else if (p->type == Rook) {
            checkIncrementalMove (p, 0, 1);
            checkIncrementalMove (p, 0,-1);
            checkIncrementalMove (p, 1, 0);
            checkIncrementalMove (p,-1, 0);
        }else if (p->type == Knight) {
            checkSingleMove (p, 2, 1);
            checkSingleMove (p, 2,-1);
            checkSingleMove (p,-2, 1);
            checkSingleMove (p,-2,-1);
            checkSingleMove (p, 1, 2);
            checkSingleMove (p,-1, 2);
            checkSingleMove (p, 1,-2);
            checkSingleMove (p,-1,-2);
        }else if (p->type == Bishop) {
            checkIncrementalMove (p, 1, 1);
            checkIncrementalMove (p,-1,-1);
            checkIncrementalMove (p, 1,-1);
            checkIncrementalMove (p,-1, 1);
        }else if (p->type == Queen) {
            checkIncrementalMove (p, 0, 1);
            checkIncrementalMove (p, 0,-1);
            checkIncrementalMove (p, 1, 0);
            checkIncrementalMove (p,-1, 0);
            checkIncrementalMove (p, 1, 1);
            checkIncrementalMove (p,-1,-1);
            checkIncrementalMove (p, 1,-1);
            checkIncrementalMove (p,-1, 1);
        }else if (p->type == King){
            checkCastling   (p);

            checkSingleMove (p,-1,-1);
            checkSingleMove (p,-1, 0);
            checkSingleMove (p,-1, 1);
            checkSingleMove (p, 0,-1);
            checkSingleMove (p, 0, 1);
            checkSingleMove (p, 1,-1);
            checkSingleMove (p, 1, 0);
            checkSingleMove (p, 1, 1);
        }
    }
    void resetBoard(bool animate = true) {
        currentPlayer = White;
        cptWhiteOut = cptBlackOut = 0;

        for (int i=0; i<32; i++) {
            Piece* p = &pieces[i];
            if (p->promoted)
                resetPromotion (p, false);
            if (p->position != p->initPosition){
                board[p->initPosition.x][p->initPosition.y] = p;
                p->position = p->initPosition;
                if (animate)
                    animatePce (p->instance, (float)p->position.x,(float)p->position.y);
            }
            p->captured = false;
            p->hasMoved = false;
        }
        for (int x=0; x<8; x++){
            if (animate){
                setCaseLight(glm::ivec2(x,0), glm::vec4(0));
                setCaseLight(glm::ivec2(x,1), glm::vec4(0));
            }
            for (int y=2; y<6; y++){
                board[x][y] = nullptr;
                if (animate)
                    setCaseLight(glm::ivec2(x,y), glm::vec4(0));
            }
            if (animate){
                setCaseLight(glm::ivec2(x,6), glm::vec4(0));
                setCaseLight(glm::ivec2(x,7), glm::vec4(0));
            }
        }
        if (animate)
            updateMiniBoard();
        movesPtr = previouMovesPtr = 24;
    }

    void startGame () {
        gameStarted = false;
        playerWin[White] = playerWin[Black] = false;

        resetBoard();
        updateMiniBoard();

        rebuildCommandBuffers();

        strncpy(movesBuffer, "position startpos moves ", 24);

        write(sfWritefd,"isready\n",8);
        //enableHint();
    }
    void switchPlayer (bool _startTurn = true) {
        if (currentPlayer==White)
            currentPlayer = Black;
        else
            currentPlayer = White;

        if (_startTurn)
            startTurn();
    }
    void clearBestMove () {
        if (bestMoveOrig.x >=0){
            subCaseLight(bestMoveOrig, bestMoveColor);
            subCaseLight(bestMoveTarget, bestMoveColor);
        }
        bestMoveOrig = bestMoveTarget = glm::vec2(-1);
    }
    void startTurn (){
        if (selectedSquare.x >= 0)
            subCaseLight(selectedSquare, selectedColor);
        if (hoverSquare.x >= 0)
            subCaseLight(hoverSquare, hoverColor);

        hoverSquare = selectedSquare = glm::vec2(-1);

        clearBestMove();

        for (int i=0; i<validMoves.size(); i++)
            subCaseLight(validMoves[i], validMoveColor);
        validMoves.clear();

        if (kingIsSafe(currentPlayer))
            setCaseLight(getKing(currentPlayer)->position, glm::vec4(0));
        else
            setCaseLight(getKing(currentPlayer)->position, checkColor);

        sendPositionsCmd();
        if (playerIsAi[currentPlayer]){
            write(sfWritefd, sfcmdGo.c_str() , sfcmdGo.length());
        }else if (hint)
            write(sfWritefd,"go infinite\n", 12);
        else
            write(sfWritefd,"go\n", 3);
    }

    void addPiece (uint32_t pIdx, const std::string& model, PceType type, Color color, int x, int y, float yAngle = 0.f) {
        pieces[pIdx].type = type;
        pieces[pIdx].color = color;
        pieces[pIdx].promoted = false;
        pieces[pIdx].position = pieces[pIdx].initPosition = glm::ivec2(x,y);
        int matIdx = -1;//default is white
        if (color == Black)
            matIdx = blackMatIdx;
        pieces[pIdx].instance =
                mod->addInstance(model,
                    glm::rotate(
                        glm::translate(glm::mat4(1.0), glm::vec3(x*2 - 7,0, 7 - y*2)),
                    yAngle, glm::vec3(0,1,0)), matIdx);
        board[x][y] = &pieces[pIdx];
    }

    int blackMatIdx = -1;
    vkglTF::Model* mod;

    const char* caseX = "abcdefgh";
    uint32_t casesInstances[8][8];


    inline glm::vec4 getCaseLight (glm::ivec2 c) {
        return mod->instanceDatas[casesInstances[c.x][c.y]].color;
    }
    void setCaseLight (glm::ivec2 c, glm::vec4 color) {
        mod->instanceDatas[casesInstances[c.x][c.y]].color = color;
        mod->setInstanceIsDirty(casesInstances[c.x][c.y]);
    }
    void setCaseRedLight (glm::ivec2 p, float c) {
        mod->instanceDatas[casesInstances[p.x][p.y]].color.x = c;
        mod->setInstanceIsDirty(casesInstances[p.x][p.y]);
    }
    inline void addCaseLight (glm::ivec2 c, glm::vec4 color) {
        addCaseLight(c.x, c.y, color);
    }
    inline void subCaseLight (glm::ivec2 c, glm::vec4 color) {
        subCaseLight(c.x, c.y, color);
    }

    void addCaseLight (uint32_t x, uint32_t y, glm::vec4 color) {
        mod->instanceDatas[casesInstances[x][y]].color += color;
        mod->setInstanceIsDirty(casesInstances[x][y]);
    }
    void subCaseLight (uint32_t x, uint32_t y, glm::vec4 color) {
        mod->instanceDatas[casesInstances[x][y]].color -= color;
        mod->setInstanceIsDirty(casesInstances[x][y]);
    }

    glm::vec3 vMouse, vMouseN;

    bool UnProject(float winX, float winY, float winZ,
                   const glm::mat4& modelView, const glm::mat4& projection,
                   float vpX, float vpY, float vpW, float vpH,
                   glm::vec3& worldCoordinates)
    {
        // Compute (projection x modelView) ^ -1:
        const glm::mat4 m = glm::inverse(modelView*projection);

        // Need to invert Y since screen Y-origin point down,
        // while 3D Y-origin points up (this is an OpenGL only requirement):
        winY = vpH - winY;

        // Transformation of normalized coordinates between -1 and 1:
        glm::vec4 in;
        in.x = (winX - vpX) / vpW  * 2.0 - 1.0;
        in.y = 2.0 * winZ - 1.0;
        in.z = (winY - vpY) / vpH * 2.0 - 1.0;
        in.w = 1.0;

        // To world coordinates:
        glm::vec4 out(in * m);
        if (out.y == 0.0) // Avoid a division by zero
        {
            worldCoordinates = glm::vec3(0);
            return false;
        }

        out.y = 1.0 / out.y;
        worldCoordinates.x = out.x * out.y;
        worldCoordinates.y = out.y * out.y;
        worldCoordinates.z = out.z * out.y;
        return true;
    }

    void drawDebugTri (glm::vec3 p, glm::vec3 color) {
//        debugRenderer->drawLine(glm::vec3(0,0,0), p, color);
//        debugRenderer->drawLine(glm::vec3(0,0,0), glm::vec3(p.x,0,p.z), color);
//        debugRenderer->drawLine(p, glm::vec3(p.x,0,p.z), color);
    }
    glm::vec3 vResult;

    virtual void handleMouseButtonDown(int butIndex) {
        if (butIndex != GLFW_MOUSE_BUTTON_LEFT)
            return;

        if (hoverSquare == selectedSquare || hoverSquare.x <0)
            return;

        if (selectedSquare.x >= 0) {
            Piece* p = board[selectedSquare.x][selectedSquare.y];
            std::vector<glm::ivec2>::iterator pos = std::find(validMoves.begin(), validMoves.end(), hoverSquare);
            if (pos!=validMoves.end()){
                //check pawn promotion
                int promoteRow = (p->color == White) ? 7 : 0;
                if (pos->y ==  promoteRow && p->type == Pawn && !p->promoted){
                    //promote dialog

                    processMove(p->position, *pos, Queen);
                }else
                    processMove(p->position, *pos);

                if (hint)
                    write(sfWritefd,"stop\n",5);
                else
                    switchPlayer();
                return;
            }
        }

        for (int i=0; i<validMoves.size(); i++)
            subCaseLight(validMoves[i], validMoveColor);
        validMoves.clear();

        if (selectedSquare.x >= 0)
            subCaseLight(selectedSquare, selectedColor);

        selectedSquare = hoverSquare;

        if (selectedSquare.x >= 0) {
            addCaseLight(selectedSquare, selectedColor);
            Piece* p = board[selectedSquare.x][selectedSquare.y];
            if (!p)
                return;
            if (p->color != currentPlayer || playerIsAi[currentPlayer])
                return;
            computeValidMove (p);
            validateMoves(p);
            for (int i=0; i<validMoves.size(); i++)
                addCaseLight(validMoves[i], validMoveColor);
        }
    }
    virtual void handleMouseMove(int32_t x, int32_t y) {
        VkEngine::handleMouseMove(x, y);

        vMouse = glm::unProject(glm::vec3(mousePos.x, mousePos.y, 0.0), mvpMatrices.view, mvpMatrices.projection,
                                  glm::vec4(0,0,width,height));

        glm::vec3 vEye = -camera.position;

        glm::vec3 vMouseRay = //vEye - vMouse;
                glm::normalize(vEye - vMouse);//- vMouseEnd);

        float t = vMouse.y/vMouseRay.y;

        glm::vec3 hoverPos = vEye - vMouseRay * t;

        glm::ivec2 newPos((int)round(hoverPos.x/2.f + 3.5f), (int)round(3.5f - hoverPos.z / 2.f));
        hoverPos.x = round(hoverPos.x);
        hoverPos.z = round(hoverPos.z);

        if (newPos.x<0||newPos.y<0||newPos.x>7||newPos.y>7){
            newPos.x = -1;
            newPos.y = -1;
        }

        if (newPos == hoverSquare)
            return;

        if (hoverSquare.x >= 0)
            subCaseLight(hoverSquare, hoverColor);

        hoverSquare = newPos;

        if (hoverSquare.x >= 0)
            addCaseLight(hoverSquare, hoverColor);

    }
    virtual void keyPressed(uint32_t key) {
        switch (key) {
        case GLFW_KEY_G://g: restart game
            startGame();
            break;
        case GLFW_KEY_R://r: redo last undo
            startGame();
            break;
        case GLFW_KEY_U://u: undo last human move
            undo();
            break;
        case GLFW_KEY_H://h
            toogleHint();
            break;
        default:
            VkEngine::keyPressed(key);
            break;
        }
    }

    virtual void prepareRenderers() {
        sceneRenderer = new pbrRenderer();

        //vkvg full screen texture
        sceneRenderer->fullScreenTex = vks::Texture(device,
                                                        vkvg_surface_get_vk_format (surf),
                                                        vkvg_surface_get_vk_image (surf),
                                                        vkvg_surface_get_width (surf),
                                                        vkvg_surface_get_height (surf));

        sceneRenderer->fullScreenTex.createView(VK_IMAGE_VIEW_TYPE_2D);
        sceneRenderer->fullScreenTex.createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                   VK_SAMPLER_MIPMAP_MODE_LINEAR);
        sceneRenderer->fullScreenTex.updateDescriptor();

        sceneRenderer->create(device, renderTarget, sharedUBOs);

        sceneRenderer->models.resize(1);
        mod = &sceneRenderer->models[0];

        mod->loadFromFile ("data/models/chess.gltf", device, true);

        mod->addInstance("frame", glm::translate(glm::mat4(1.0),       glm::vec3( 0,0,0)));

        blackMatIdx = mod->getMaterialIndex("black");

        addPiece(00, "rook",  Rook, White,    0, 0);
        addPiece(07, "rook",  Rook, White,    7, 0);
        addPiece(16, "rook",  Rook, Black,    0, 7);
        addPiece(23, "rook",  Rook, Black,    7, 7);
        addPiece(01, "knight",Knight, White,  1, 0);
        addPiece(06, "knight",Knight, White,  6, 0);
        addPiece(17, "knight",Knight, Black,  1, 7, M_PI);
        addPiece(22, "knight",Knight, Black,  6, 7, M_PI);
        addPiece(02, "bishop",Bishop, White,  2, 0);
        addPiece(05, "bishop",Bishop, White,  5, 0);
        addPiece(18, "bishop",Bishop, Black,  2, 7);
        addPiece(21, "bishop",Bishop, Black,  5, 7);
        addPiece(03, "queen", Queen, White,   3, 0);
        addPiece(19, "queen", Queen, Black,   3, 7);
        addPiece(04, "king",  King, White,    4, 0);
        addPiece(20, "king",  King, Black,    4, 7);

        for (int i=0; i<8; i++)
            addPiece(8 + i, "pawn", Pawn, White, i, 1);
        for (int i=0; i<8; i++)
            addPiece(24 + i, "pawn", Pawn, Black, i, 6);

        for (int y=0; y<8; y++)
            for (int x=0; x<8; x++)
                casesInstances[x][y] = mod->addInstance(caseX[x] + std::to_string(y+1) + "\0",
                                                        glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));

        sceneRenderer->prepareModels();
        sceneRenderer->buildCommandBuffer();

//        debugRenderer = new vks::vkRenderer ();
//        debugRenderer->create(device, renderTarget, sharedUBOs);
//        //debugRenderer->clear();
//        debugRenderer->drawLine(glm::vec3(0,0,0), glm::vec3(1,0,0), glm::vec3(1,0,0));
//        debugRenderer->drawLine(glm::vec3(0,0,0), glm::vec3(0,1,0), glm::vec3(0,1,0));
//        debugRenderer->drawLine(glm::vec3(0,0,0), glm::vec3(0,0,1), glm::vec3(0,0,1));
//        debugRenderer->flush();
    }
    VkvgSurface piecesImgs[2][6];

    virtual void prepare() {

        vkvgDev  = vkvg_device_create (device->phy, device->dev, phyInfos.gQueues[0], 0);
        surf    = vkvg_surface_create(vkvgDev, width, height);
        vkvg_surface_clear(surf);

        piecesImgs[White][King] = vkvg_surface_create_from_image(vkvgDev, "data/wk.png");
        piecesImgs[White][Queen] = vkvg_surface_create_from_image(vkvgDev, "data/wq.png");
        piecesImgs[White][Bishop] = vkvg_surface_create_from_image(vkvgDev, "data/wb.png");
        piecesImgs[White][Rook] = vkvg_surface_create_from_image(vkvgDev, "data/wr.png");
        piecesImgs[White][Knight] = vkvg_surface_create_from_image(vkvgDev, "data/wn.png");
        piecesImgs[White][Pawn] = vkvg_surface_create_from_image(vkvgDev, "data/wp.png");
        piecesImgs[Black][King] = vkvg_surface_create_from_image(vkvgDev, "data/bk.png");
        piecesImgs[Black][Queen] = vkvg_surface_create_from_image(vkvgDev, "data/bq.png");
        piecesImgs[Black][Bishop] = vkvg_surface_create_from_image(vkvgDev, "data/bb.png");
        piecesImgs[Black][Rook] = vkvg_surface_create_from_image(vkvgDev, "data/br.png");
        piecesImgs[Black][Knight] = vkvg_surface_create_from_image(vkvgDev, "data/bn.png");
        piecesImgs[Black][Pawn] = vkvg_surface_create_from_image(vkvgDev, "data/bp.png");

        prepareRenderers();

        startStockFish();

        startGame();
    }
    virtual void windowResize() {
        VkEngine::windowResize();
        rebuildCommandBuffers();
    }

    void rebuildCommandBuffers() {
        sceneRenderer->rebuildCommandBuffer();
    }

    void render () {
        if (!prepared)
            return;

        prepareFrame();

        sceneRenderer->submit(device->queue, &swapChain->presentCompleteSemaphore, 1);
        //VK_CHECK_RESULT(swapChain.queuePresent(queue, this->drawComplete));

        //debugRenderer->submit(vulkanDevice->queue,&sceneRenderer->drawComplete, 1);
        VK_CHECK_RESULT(swapChain->queuePresent(device->queue, sceneRenderer->drawComplete));

        update();
    }
};

VkChess *vkChess;


int main(const int argc, const char *argv[])
{

    for (size_t i = 0; i < argc; i++) { VkChess::args.push_back(argv[i]); };
    vkChess = new VkChess();
    vkChess->start();
    delete(vkChess);
    return 0;
}
