package android.media;
public class SoundPool {
	public interface OnLoadCompleteListener { void onLoadComplete(SoundPool p,int sampleId,int status); }
	public SoundPool(int max,int stream,int q){}
	public void release(){} public void autoPause(){} public void autoResume(){}
	public void setOnLoadCompleteListener(OnLoadCompleteListener l){}
	public int load(android.content.res.AssetFileDescriptor fd,int prio){ return 0; }
	public boolean unload(int id){ return true; }
	public int play(int id,float l,float r,int prio,int loop,float rate){ return 0; }
}
