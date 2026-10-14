package android.graphics;
public class RectF {
	public float left, top, right, bottom;
	public RectF(){} public RectF(float l,float t,float r,float b){ left=l; top=t; right=r; bottom=b; }
	public RectF(RectF r){ if(r!=null) set(r); }
	public void set(float l,float t,float r,float b){ left=l; top=t; right=r; bottom=b; }
	public void set(RectF r){ set(r.left,r.top,r.right,r.bottom); }
	public final float width(){ return right-left; } public final float height(){ return bottom-top; }
	public final float centerX(){ return (left+right)/2; } public final float centerY(){ return (top+bottom)/2; }
	public void offset(float dx,float dy){ left+=dx; right+=dx; top+=dy; bottom+=dy; }
	public void offsetTo(float x,float y){ right+=x-left; bottom+=y-top; left=x; top=y; }
	public void inset(float dx,float dy){ left+=dx; top+=dy; right-=dx; bottom-=dy; }
	public boolean contains(float x,float y){ return left<right && top<bottom && x>=left && x<right && y>=top && y<bottom; }
	public boolean isEmpty(){ return left>=right || top>=bottom; }
	public void union(float x,float y){ if(x<left)left=x; else if(x>right)right=x; if(y<top)top=y; else if(y>bottom)bottom=y; }
}
