package org.json;
import java.util.*;
public class JSONArray {
	final ArrayList<Object> list = new ArrayList<>();
	public int length(){ return list.size(); }
	private void set(int i,Object v){ while(list.size()<=i) list.add(null); list.set(i,v); }
	public JSONArray put(Object v){ list.add(v); return this; }
	public JSONArray put(int i,int v) throws JSONException { set(i,v); return this; }
	public JSONArray put(int i,boolean v) throws JSONException { set(i,v); return this; }
	public JSONArray put(int i,Object v) throws JSONException { set(i,v); return this; }
	public int getInt(int i) throws JSONException { return ((Number)list.get(i)).intValue(); }
	public boolean getBoolean(int i) throws JSONException { return (Boolean)list.get(i); }
	public String getString(int i) throws JSONException { return String.valueOf(list.get(i)); }
	public JSONObject getJSONObject(int i) throws JSONException { Object v=list.get(i); if(!(v instanceof JSONObject)) throw new JSONException("idx"); return (JSONObject)v; }
	public String toString(){ return list.toString(); }
}
