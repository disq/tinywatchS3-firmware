#pragma once

#include "tw_faces/tw_face.h"

class FaceWatch_DefaultDigital : public tw_face
{
    public:
		void setup(void);
		void draw(bool force);
		bool click(uint pos_x, uint pos_y);
		bool click_double(uint pos_x, uint pos_y);
		bool click_long(uint pos_x, uint pos_y);

    private:
        String version = "1.0";

};

extern FaceWatch_DefaultDigital face_watch_default_digital;
