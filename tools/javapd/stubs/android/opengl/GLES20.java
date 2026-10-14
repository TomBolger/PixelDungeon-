package android.opengl;
import java.nio.Buffer;
/** Headless stub: every GL call is a no-op. */
public class GLES20 {
	public static final int GL_ALPHA=0x1906, GL_CLAMP_TO_EDGE=0x812F, GL_COLOR_ATTACHMENT0=0x8CE0, GL_COLOR_BUFFER_BIT=0x4000,
		GL_COMPILE_STATUS=0x8B81, GL_DEPTH_ATTACHMENT=0x8D00, GL_DEPTH_COMPONENT16=0x81A5, GL_FALSE=0, GL_TRUE=1, GL_FLOAT=0x1406,
		GL_FRAGMENT_SHADER=0x8B30, GL_FRAMEBUFFER=0x8D40, GL_FRAMEBUFFER_COMPLETE=0x8CD5, GL_LINEAR=0x2601, GL_LINK_STATUS=0x8B82,
		GL_MIRRORED_REPEAT=0x8370, GL_NEAREST=0x2600, GL_RENDERBUFFER=0x8D41, GL_REPEAT=0x2901, GL_RGBA=0x1908,
		GL_STENCIL_ATTACHMENT=0x8D20, GL_STENCIL_INDEX8=0x8D48, GL_TEXTURE0=0x84C0, GL_TEXTURE_2D=0x0DE1,
		GL_TEXTURE_MAG_FILTER=0x2800, GL_TEXTURE_MIN_FILTER=0x2801, GL_TEXTURE_WRAP_S=0x2802, GL_TEXTURE_WRAP_T=0x2803,
		GL_TRIANGLES=4, GL_UNPACK_ALIGNMENT=0x0CF5, GL_UNSIGNED_BYTE=0x1401, GL_UNSIGNED_SHORT=0x1403, GL_VERTEX_SHADER=0x8B31;
	private static int nextId = 1;
	private static void gen(int n, int[] a, int off){ for(int i=0;i<n;i++) a[off+i]=nextId++; }
	public static void glActiveTexture(int t){}
	public static void glAttachShader(int p,int s){}
	public static void glBindFramebuffer(int a,int b){}
	public static void glBindRenderbuffer(int a,int b){}
	public static void glBindTexture(int a,int b){}
	public static void glBlendFunc(int a,int b){}
	public static void glClear(int m){}
	public static void glCompileShader(int s){}
	public static int glCreateProgram(){ return nextId++; }
	public static int glCreateShader(int t){ return nextId++; }
	public static int glCheckFramebufferStatus(int t){ return GL_FRAMEBUFFER_COMPLETE; }
	public static void glDeleteFramebuffers(int n,int[] a,int o){}
	public static void glDeleteProgram(int p){}
	public static void glDeleteRenderbuffers(int n,int[] a,int o){}
	public static void glDeleteShader(int s){}
	public static void glDeleteTextures(int n,int[] a,int o){}
	public static void glDisableVertexAttribArray(int l){}
	public static void glEnableVertexAttribArray(int l){}
	public static void glDrawElements(int m,int c,int t,Buffer b){}
	public static void glDrawElements(int m,int c,int t,int off){}
	public static void glEnable(int c){}
	public static void glFramebufferRenderbuffer(int a,int b,int c,int d){}
	public static void glFramebufferTexture2D(int a,int b,int c,int d,int e){}
	public static void glGenBuffers(int n,int[] a,int o){ gen(n,a,o); }
	public static void glGenRenderbuffers(int n,int[] a,int o){ gen(n,a,o); }
	public static void glGenFramebuffers(int n,int[] a,int o){ gen(n,a,o); }
	public static void glGenTextures(int n,int[] a,int o){ gen(n,a,o); }
	public static int glGetAttribLocation(int p,String n){ return 0; }
	public static int glGetUniformLocation(int p,String n){ return 0; }
	public static void glGetProgramiv(int p,int k,int[] a,int o){ a[o]=GL_TRUE; }
	public static void glGetShaderiv(int s,int k,int[] a,int o){ a[o]=GL_TRUE; }
	public static String glGetProgramInfoLog(int p){ return ""; }
	public static String glGetShaderInfoLog(int s){ return ""; }
	public static void glLinkProgram(int p){}
	public static void glPixelStorei(int a,int b){}
	public static void glRenderbufferStorage(int a,int b,int c,int d){}
	public static void glScissor(int a,int b,int c,int d){}
	public static void glShaderSource(int s,String src){}
	public static void glTexImage2D(int a,int b,int c,int d,int e,int f,int g,int h,Buffer buf){}
	public static void glTexParameterf(int a,int b,float c){}
	public static void glTexParameteri(int a,int b,int c){}
	public static void glUniform1f(int l,float v){}
	public static void glUniform1i(int l,int v){}
	public static void glUniform2f(int l,float a,float b){}
	public static void glUniform4f(int l,float a,float b,float c,float d){}
	public static void glUniformMatrix3fv(int l,int c,boolean t,float[] v,int o){}
	public static void glUniformMatrix4fv(int l,int c,boolean t,float[] v,int o){}
	public static void glUseProgram(int p){}
	public static void glVertexAttribPointer(int l,int s,int t,boolean n,int st,Buffer b){}
	public static void glVertexAttribPointer(int l,int s,int t,boolean n,int st,int off){}
	public static void glViewport(int a,int b,int c,int d){}
}
