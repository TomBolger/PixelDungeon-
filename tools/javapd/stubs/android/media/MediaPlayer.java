package android.media;
public class MediaPlayer {
	public interface OnPreparedListener { void onPrepared(MediaPlayer mp); }
	public interface OnErrorListener { boolean onError(MediaPlayer mp,int what,int extra); }
	public void setAudioStreamType(int t){}
	public void setDataSource(java.io.FileDescriptor fd,long off,long len) throws java.io.IOException {}
	public void setOnPreparedListener(OnPreparedListener l){} public void setOnErrorListener(OnErrorListener l){}
	public void setLooping(boolean b){} public void prepareAsync(){} public void start(){} public void pause(){} public void stop(){} public void release(){}
	public void setVolume(float l,float r){} public boolean isPlaying(){ return false; }
}
