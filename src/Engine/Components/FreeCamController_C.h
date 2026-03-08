struct FreeCamController_C
{
    float moveSpeed_ = 10.0f;
    float boostSpeed_ = 30.0f;
    float mouseSensitivity_ = 0.0025f;

    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    bool requireRightMouseButton_ = false;
    bool controlled_ = true;
};
