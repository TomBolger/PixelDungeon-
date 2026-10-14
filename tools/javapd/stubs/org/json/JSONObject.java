package org.json;
import java.util.*;
/** Minimal in-memory JSONObject (no parsing; toString is approximate). */
public class JSONObject {
	final LinkedHashMap<String,Object> map = new LinkedHashMap<>();
	public JSONObject(){}
	public Iterator<String> keys(){ return map.keySet().iterator(); }
	public boolean isNull(String k){ return !map.containsKey(k) || map.get(k)==null; }
	public JSONObject put(String k,boolean v) throws JSONException { map.put(k,v); return this; }
	public JSONObject put(String k,int v) throws JSONException { map.put(k,v); return this; }
	public JSONObject put(String k,long v) throws JSONException { map.put(k,v); return this; }
	public JSONObject put(String k,double v) throws JSONException { map.put(k,v); return this; }
	public JSONObject put(String k,Object v) throws JSONException { map.put(k,v); return this; }
	public Object opt(String k){ return map.get(k); }
	public boolean optBoolean(String k){ Object v=map.get(k); return v instanceof Boolean?(Boolean)v:false; }
	public int optInt(String k){ Object v=map.get(k); return v instanceof Number?((Number)v).intValue():0; }
	public double optDouble(String k){ Object v=map.get(k); return v instanceof Number?((Number)v).doubleValue():Double.NaN; }
	public String optString(String k){ Object v=map.get(k); return v==null?"":v.toString(); }
	public JSONObject optJSONObject(String k){ Object v=map.get(k); return v instanceof JSONObject?(JSONObject)v:null; }
	public String getString(String k) throws JSONException { Object v=map.get(k); if(v==null) throw new JSONException(k); return v.toString(); }
	public JSONArray getJSONArray(String k) throws JSONException { Object v=map.get(k); if(!(v instanceof JSONArray)) throw new JSONException(k); return (JSONArray)v; }
	public JSONObject getJSONObject(String k) throws JSONException { Object v=map.get(k); if(!(v instanceof JSONObject)) throw new JSONException(k); return (JSONObject)v; }
	public String toString(){ StringBuilder sb=new StringBuilder("{"); boolean f=true; for(Map.Entry<String,Object> e:map.entrySet()){ if(!f)sb.append(','); f=false; sb.append('"').append(e.getKey()).append("\":"); Object v=e.getValue(); sb.append(v instanceof String?"\""+v+"\"":String.valueOf(v)); } return sb.append('}').toString(); }
}
