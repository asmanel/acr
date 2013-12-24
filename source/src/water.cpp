#include "pch.h"
#include "cube.h"

int wx1, wy1, wx2, wy2;
float wsx1, wsy1, wsx2, wsy2;

inline void sendwater(){ addmsg(N_EDITW, "ri5", hdr.waterlevel, hdr.watercolor[0], hdr.watercolor[1], hdr.watercolor[2], hdr.watercolor[3]); }

void setwaterlevel(int w){
	hdr.waterlevel = w;
	if(noteditmode("waterlevel")) return;
	sendwater();
}

VARP(watersubdiv, 1, 4, 64);
VARF(waterlevel, -128, -128, 127, setwaterlevel(waterlevel));

void setwatercolor(const char *r, const char *g, const char *b, const char *a)
{
	if(r && b && g && *r){
		hdr.watercolor[0] = ATOI(r);
		hdr.watercolor[1] = ATOI(g);
		hdr.watercolor[2] = ATOI(b);
		hdr.watercolor[3] = a[0] ? ATOI(a) : 178;
		if(noteditmode("watercolor")) return;
		sendwater();
	}
	else{
		hdr.watercolor[0] = 20;
		hdr.watercolor[1] = 25;
		hdr.watercolor[2] = 20;
		hdr.watercolor[3] = 178;
	}
}

COMMANDN(watercolor, setwatercolor, ARG_4STR);

// renders water for bounding rect area that contains water... simple but very inefficient

#define VERTW(vertw, body) \
	inline void vertw(float v1, float v2, float v3, float t) \
	{ \
		float angle = v1*v2*0.1f + t; \
		float h = 0.3f*sinf(angle); \
		body; \
		glVertex3f(v1, v2, v3+h); \
	}
#define VERTWT(vertwt, body) VERTW(vertwt, { float v = cosf(angle); float duv = 0.2f*v; body; })
VERTW(vertw, {})
VERTW(vertwc, {
	float v = cosf(angle);
	glColor4ub(hdr.watercolor[0], hdr.watercolor[1], hdr.watercolor[2], (uchar)(hdr.watercolor[3] + (max(v, 0.0f) - 0.5f)*51.0f));
})
VERTWT(vertwt, {
	glTexCoord3f(v1+duv, v2+duv, v3+h);
})
VERTWT(vertwtc, {
	glColor4f(1, 1, 1, 0.15f + max(v, 0.0f)*0.15f);
	glTexCoord3f(v1+duv, v2+duv, v3+h);
})
VERTWT(vertwmtc, {
	glColor4f(1, 1, 1, 0.15f + max(v, 0.0f)*0.15f);
	glMultiTexCoord3f_(GL_TEXTURE0_ARB, v1-duv, v2+duv, v3+h);
	glMultiTexCoord3f_(GL_TEXTURE1_ARB, v1+duv, v2+duv, v3+h);
})

extern int nquads;

#define renderwaterstrips(vertw, hf, t) \
	for(int x = wx1; x<wx2; x += watersubdiv) \
	{ \
		glBegin(GL_TRIANGLE_STRIP); \
		vertw(x,			 wy1, hf, t); \
		vertw(x+watersubdiv, wy1, hf, t); \
		for(int y = wy1; y<wy2; y += watersubdiv) \
		{ \
			vertw(x,			 y+watersubdiv, hf, t); \
			vertw(x+watersubdiv, y+watersubdiv, hf, t); \
		} \
		glEnd(); \
		nquads += (wy2-wy1-1)/watersubdiv; \
	}

void setprojtexmatrix()
{
	glmatrixf projtex = mvpmatrix;
	projtex.projective();

	glMatrixMode(GL_TEXTURE);
	glLoadMatrixf(projtex.v);
}

void setupmultitexrefract(GLuint reflecttex, GLuint refracttex)
{
	setuptmu(0, "K , T @ Ka");
	
	colortmu(0, hdr.watercolor[0]/255.0f, hdr.watercolor[1]/255.0f, hdr.watercolor[2]/255.0f, hdr.watercolor[3]/255.0f);

	glBindTexture(GL_TEXTURE_2D, refracttex);
	setprojtexmatrix();

	glActiveTexture_(GL_TEXTURE1_ARB);
	glEnable(GL_TEXTURE_2D);
	
	setuptmu(1, "P , T @ C~a");
   
	glBindTexture(GL_TEXTURE_2D, reflecttex);
	setprojtexmatrix();

	glActiveTexture_(GL_TEXTURE0_ARB);
}

void setupmultitexreflect(GLuint reflecttex)
{
	setuptmu(0, "T , K @ Ca", "Ka * P~a");
	
	float a = hdr.watercolor[3]/255.0f;
	colortmu(0, hdr.watercolor[0]/255.0f*a, hdr.watercolor[1]/255.0f*a, hdr.watercolor[2]/255.0f*a, 1.0f-a);

	glBindTexture(GL_TEXTURE_2D, reflecttex);
	setprojtexmatrix();
}

void cleanupmultitex(GLuint reflecttex, GLuint refracttex)
{
	resettmu(0);
	glLoadIdentity();
   
	if(refracttex)
	{ 
		glActiveTexture_(GL_TEXTURE1_ARB);
		glDisable(GL_TEXTURE_2D);
		resettmu(1);
		glLoadIdentity();
		glActiveTexture_(GL_TEXTURE0_ARB);
	}
	glMatrixMode(GL_MODELVIEW);
}

VARP(mtwater, 0, 1, 1);

int renderwater(float hf, GLuint reflecttex, GLuint refracttex)
{
	if(wx1<0) return nquads;
	
	wx1 -= wx1%watersubdiv;
	wy1 -= wy1%watersubdiv;

	float t = lastmillis/300.0f;

	if(mtwater && maxtmus>=2 && reflecttex)
	{
		if(refracttex)
		{
			setupmultitexrefract(reflecttex, refracttex);		
			renderwaterstrips(vertwmtc, hf, t);
		}
		else
		{
			setupmultitexreflect(reflecttex);
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_SRC_ALPHA);
			renderwaterstrips(vertwtc, hf, t);
			glDisable(GL_BLEND);
			glDepthMask(GL_TRUE);
		}
		cleanupmultitex(reflecttex, refracttex);
		
		return nquads;
	}

	if(!refracttex) 
	{
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
	}
	glDepthMask(GL_FALSE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if(reflecttex)
	{
		if(!refracttex)
		{
			glColor4ubv(hdr.watercolor);
			renderwaterstrips(vertw, hf, t);
		
			glEnable(GL_TEXTURE_2D);
		}

		setprojtexmatrix();

		glBindTexture(GL_TEXTURE_2D, refracttex ? refracttex : reflecttex);
	}

	if(refracttex) 
	{
		glColor3f(1, 1, 1);
		renderwaterstrips(vertwt, hf, t);
		glEnable(GL_BLEND);

		glBindTexture(GL_TEXTURE_2D, reflecttex);
		glDepthMask(GL_TRUE);
	}
	if(reflecttex) { renderwaterstrips(vertwtc, hf, t); }
	else { renderwaterstrips(vertwc, hf, t); }

	if(reflecttex)
	{
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
	}
	else glEnable(GL_TEXTURE_2D);

	glDisable(GL_BLEND);
	if(!refracttex) glDepthMask(GL_TRUE);
   
	return nquads;
}

void addwaterquad(int x, int y, int size)	   // update bounding rect that contains water
{
	int x2 = x+size;
	int y2 = y+size;
	if(wx1<0)
	{
		wx1 = x;
		wy1 = y;
		wx2 = x2;
		wy2 = y2;
	}
	else
	{
		if(x<wx1) wx1 = x;
		if(y<wy1) wy1 = y;
		if(x2>wx2) wx2 = x2;
		if(y2>wy2) wy2 = y2;
	}
}

void calcwaterscissor()
{
	vec4 v[4];
	float sx1 = 1, sy1 = 1, sx2 = -1, sy2 = -1;
	loopi(4)
	{
		vec4 &p = v[i];
		mvpmatrix.transform(vec(i&1 ? wx2 : wx1, i&2 ? wy2 : wy1, hdr.waterlevel-0.3f), p);
		if(p.z >= 0)
		{
			float x = p.x / p.w, y = p.y / p.w;
			sx1 = min(sx1, x);
			sy1 = min(sy1, y);
			sx2 = max(sx2, x);
			sy2 = max(sy2, y);
		}
	}
	if(sx1 >= sx2 || sy1 >= sy2) 
	{
		sx1 = sy1 = -1;
		sx2 = sy2 = 1;
	}
	else loopi(4)
	{
		const vec4 &p = v[i];
		if(p.z >= 0) continue;
		loopj(2)
		{
			const vec4 &o = v[i^(1<<j)];
			if(o.z <= 0) continue;
			float t = p.z/(p.z - o.z),
				  w = p.w + t*(o.w - p.w),
				  x = (p.x + t*(o.x - p.x))/w,
				  y = (p.y + t*(o.y - p.y))/w;
			sx1 = min(sx1, x);
			sy1 = min(sy1, y);
			sx2 = max(sx2, x);
			sy2 = max(sy2, y);
		}
	}
	wsx1 = max(sx1, -1.0f);
	wsy1 = max(sy1, -1.0f);
	wsx2 = min(sx2, 1.0f);
	wsy2 = min(sy2, 1.0f);
}

void resetwater()
{
	if(!reflecting) wx1 = -1;
}

