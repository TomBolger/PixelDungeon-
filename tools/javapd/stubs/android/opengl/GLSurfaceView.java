package android.opengl;
import javax.microedition.khronos.opengles.GL10;
import javax.microedition.khronos.egl.EGLConfig;
public class GLSurfaceView extends android.view.View {
	public interface Renderer { void onDrawFrame(GL10 gl); void onSurfaceChanged(GL10 gl,int w,int h); void onSurfaceCreated(GL10 gl,EGLConfig c); }
	public GLSurfaceView(android.content.Context c){ super(c); }
	public void setEGLContextClientVersion(int v){}
	public void setEGLConfigChooser(boolean b){}
	public void setEGLConfigChooser(int r,int g,int b,int a,int d,int s){}
	public void setRenderer(Renderer r){}
	public void onResume(){} public void onPause(){}
	public android.view.SurfaceHolder getHolder(){ return null; }
}
