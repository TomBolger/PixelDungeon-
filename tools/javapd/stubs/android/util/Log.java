package android.util;
public class Log {
	public static boolean ENABLED = false;
	public static int i(String t,String m){ if(ENABLED) System.out.println("["+t+"] "+m); return 0; }
	public static int e(String t,String m){ System.err.println("[E/"+t+"] "+m); return 0; }
	public static int w(String t,String m){ return 0; }
	public static int d(String t,String m){ return 0; }
	public static String getStackTraceString(Throwable tr){ java.io.StringWriter w=new java.io.StringWriter(); tr.printStackTrace(new java.io.PrintWriter(w)); return w.toString(); }
}
