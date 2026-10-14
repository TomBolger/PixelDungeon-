package android.graphics;
/** Headless fake bitmap: has a size and an int[] of pixels, nothing else. */
public class Bitmap {
	public enum Config { ALPHA_8, RGB_565, ARGB_4444, ARGB_8888 }
	public static int DEFAULT_SIZE = 256;
	private int w, h; private int[] px;
	public Bitmap(int w,int h){ this.w=w; this.h=h; px=new int[Math.max(1,w*h)]; }
	public static Bitmap createBitmap(int w,int h,Config c){ return new Bitmap(w,h); }
	public static Bitmap createBitmap(Bitmap src,int x,int y,int w,int h){ return new Bitmap(w,h); }
	public int getWidth(){ return w; } public int getHeight(){ return h; }
	public int getPixel(int x,int y){ return (x>=0&&y>=0&&x<w&&y<h)?px[y*w+x]:0; }
	public void setPixel(int x,int y,int c){ if(x>=0&&y>=0&&x<w&&y<h) px[y*w+x]=c; }
	public void getPixels(int[] out,int off,int stride,int x,int y,int ww,int hh){ for(int j=0;j<hh;j++) for(int i=0;i<ww;i++) out[off+j*stride+i]=getPixel(x+i,y+j); }
	public void setPixels(int[] in,int off,int stride,int x,int y,int ww,int hh){}
	public void eraseColor(int c){ java.util.Arrays.fill(px,c); }
	public void recycle(){} public boolean isRecycled(){ return false; }
	public Config getConfig(){ return Config.ARGB_8888; }
	public void copyPixelsToBuffer(java.nio.Buffer b){}
}
