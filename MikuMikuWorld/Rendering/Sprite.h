#pragma once
#include <string>

namespace MikuMikuWorld
{
	class Sprite
	{
	  private:
		float x, y, width, height;
		std::string texture;

	  public:
		Sprite(const std::string& tex, float _x, float _y, float _w, float _h);

		float getX() const;
		float getY() const;
		float getWidth() const;
		float getHeight() const;		
		// Compatibility methods for MMW-style API
		inline float getX1() const { return x; }
		inline float getY1() const { return y; }
		inline float getX2() const { return x + width; }
		inline float getY2() const { return y + height; }	};
}
