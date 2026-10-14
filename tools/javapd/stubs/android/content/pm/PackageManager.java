package android.content.pm;
public class PackageManager {
	public static class NameNotFoundException extends Exception {}
	public PackageInfo getPackageInfo(String n,int f) throws NameNotFoundException { return new PackageInfo(); }
}
