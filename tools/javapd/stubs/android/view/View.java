package android.view;
public class View {
	public static final int SYSTEM_UI_FLAG_LAYOUT_STABLE=0x100, SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION=0x200, SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN=0x400,
		SYSTEM_UI_FLAG_HIDE_NAVIGATION=2, SYSTEM_UI_FLAG_FULLSCREEN=4, SYSTEM_UI_FLAG_IMMERSIVE_STICKY=0x1000;
	public interface OnTouchListener { boolean onTouch(View v,MotionEvent e); }
	public View(){} public View(android.content.Context c){}
	public void setOnTouchListener(OnTouchListener l){} public void setSystemUiVisibility(int f){}
}
