package android.graphics;
public class Rect {
	public int left, top, right, bottom;
	public Rect(){} public Rect(int l,int t,int r,int b){ left=l; top=t; right=r; bottom=b; }
	public void set(int l,int t,int r,int b){ left=l; top=t; right=r; bottom=b; }
	public final int width(){ return right-left; } public final int height(){ return bottom-top; }
}
