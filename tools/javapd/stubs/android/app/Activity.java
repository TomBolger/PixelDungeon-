package android.app;
import android.content.Context;
import android.content.SharedPreferences;
public class Activity extends Context {
	private final SharedPreferences prefs = new SharedPreferences();
	protected void onCreate(android.os.Bundle b){} public void onResume(){} public void onPause(){} public void onDestroy(){}
	public boolean onKeyDown(int k,android.view.KeyEvent e){ return false; }
	public boolean onKeyUp(int k,android.view.KeyEvent e){ return false; }
	public SharedPreferences getPreferences(int mode){ return prefs; }
	public android.view.WindowManager getWindowManager(){ return new android.view.WindowManager(); }
	public android.view.Window getWindow(){ return new android.view.Window(); }
	public void setVolumeControlStream(int s){} public void setContentView(android.view.View v){}
	public void setRequestedOrientation(int o){} public void runOnUiThread(Runnable r){ r.run(); }
	public void finish(){} public void onWindowFocusChanged(boolean f){} public void onBackPressed(){}
}
