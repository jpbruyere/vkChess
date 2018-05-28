#pragma once

glm::mat4 btTransformToGlmMat (const btTransform &trans){
	btVector3 o = trans.getOrigin();

	btQuaternion btQ = trans.getRotation();
	glm::quat q = glm::quat (btQ.getW(), btQ.getX(), btQ.getY(), btQ.getZ());
	return glm::translate(glm::mat4(1.0),glm::vec3(o.getX(), o.getY(), o.getZ())) * glm::mat4(q);
}
