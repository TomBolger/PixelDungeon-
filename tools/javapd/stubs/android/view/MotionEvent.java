package android.view;
public class MotionEvent {
	public static final int ACTION_DOWN=0, ACTION_UP=1, ACTION_MOVE=2, ACTION_CANCEL=3, ACTION_POINTER_DOWN=5, ACTION_POINTER_UP=6, ACTION_MASK=0xff;
	public static MotionEvent obtain(MotionEvent e){ return e; }
	public int getAction(){ return 0; } public int getActionIndex(){ return 0; } public int getPointerId(int i){ return 0; }
	public int getPointerCount(){ return 0; } public float getX(int i){ return 0; } public float getY(int i){ return 0; }
	public void recycle(){}
}
