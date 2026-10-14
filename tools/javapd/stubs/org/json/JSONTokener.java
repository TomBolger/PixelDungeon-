package org.json;
/** Parsing not supported headless (save/load is not needed for simulations). */
public class JSONTokener { public JSONTokener(String s){} public Object nextValue() throws JSONException { throw new JSONException("JSON parsing not supported in headless stub"); } }
