#pragma once

namespace MikuMikuWorld
{
	class Framebuffer
	{
	  private:
		unsigned int fbo;
		unsigned int rbo;
		unsigned int buffer;
		unsigned int width;
		unsigned int height;

		void setup();
		void createTexture(unsigned int tex);

	  public:
		Framebuffer(unsigned int w, unsigned int h);
		Framebuffer();

		void clear();
	void clear(float r, float g, float b, float a); // MMW compatibility - clear with color
		void bind();
		void unblind(); // MMW compatibility - unbind framebuffer
		void dispose();
		void resize(unsigned int w, unsigned int h);

		unsigned int getWidth() const;
		unsigned int getHeight() const;
		unsigned int getTexture() const;
	};
}