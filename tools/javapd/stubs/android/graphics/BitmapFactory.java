package android.graphics;
import java.io.InputStream;
public class BitmapFactory {
	public static class Options { public boolean inDither, inScaled=true, inPurgeable; public Bitmap.Config inPreferredConfig; }
	public static Bitmap decodeStream(InputStream s){ return new Bitmap(Bitmap.DEFAULT_SIZE,Bitmap.DEFAULT_SIZE); }
	public static Bitmap decodeStream(InputStream s,Rect r,Options o){ return decodeStream(s); }
	public static Bitmap decodeResource(android.content.res.Resources res,int id){ return decodeStream(null); }
	public static Bitmap decodeResource(android.content.res.Resources res,int id,Options o){ return decodeStream(null); }
}
