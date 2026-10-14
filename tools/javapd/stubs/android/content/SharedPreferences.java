package android.content;
import java.util.HashMap;
/** In-memory preferences. Concrete class (Android's is an interface; only used as a type here). */
public class SharedPreferences {
	private final HashMap<String,Object> map = new HashMap<>();
	public int getInt(String k,int d){ Object v=map.get(k); return v instanceof Integer?(Integer)v:d; }
	public boolean getBoolean(String k,boolean d){ Object v=map.get(k); return v instanceof Boolean?(Boolean)v:d; }
	public String getString(String k,String d){ Object v=map.get(k); return v instanceof String?(String)v:d; }
	public Editor edit(){ return new Editor(); }
	public class Editor {
		public Editor putInt(String k,int v){ map.put(k,v); return this; }
		public Editor putBoolean(String k,boolean v){ map.put(k,v); return this; }
		public Editor putString(String k,String v){ map.put(k,v); return this; }
		public boolean commit(){ return true; } public void apply(){}
	}
}
