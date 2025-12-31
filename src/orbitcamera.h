#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

struct OrbitCamera
{
    glm::vec3 target;   // point we orbit around
    float distance;     // distance from target
    float yaw;          // radians
    float pitch;        // radians

    float fov;          // radians
    float nearClip;
    float farClip;
};

void orbitcamera_initialize(OrbitCamera *camera);

glm::vec3 orbitcamera_position(OrbitCamera *cam);

glm::mat4 orbitcamera_view(OrbitCamera *cam);

glm::mat4 orbitcamera_proj(OrbitCamera *cam, float aspect);

void orbitcamera_rotate(
    OrbitCamera *cam,
    float deltaX,
    float deltaY,
    float sensitivity = 0.005f);

void orbitcamera_zoom(
    OrbitCamera *cam,
    float scrollDelta,
    float zoomSpeed = 0.5f);
