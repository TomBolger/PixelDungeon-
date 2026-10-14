package android.content;
import android.content.res.AssetManager;
import android.content.res.Resources;
public class Context {
	public static final int MODE_PRIVATE = 0;
	public static final String VIBRATOR_SERVICE = "vibrator";
	private static final AssetManager ASSETS = new AssetManager();
	private static final Resources RES = new Resources();
	public AssetManager getAssets(){ return ASSETS; }
	public Resources getResources(){ return RES; }
	public Object getSystemService(String n){ return new android.os.Vibrator(); }
	public String getPackageName(){ return "com.watabou.pixeldungeon"; }
	public android.content.pm.PackageManager getPackageManager(){ return new android.content.pm.PackageManager(); }
	public java.io.FileInputStream openFileInput(String n) throws java.io.FileNotFoundException { throw new java.io.FileNotFoundException(n); }
	public java.io.FileOutputStream openFileOutput(String n,int mode) throws java.io.FileNotFoundException { throw new java.io.FileNotFoundException(n); }
	public boolean deleteFile(String n){ return false; }
	public void startActivity(Intent i){}
}
