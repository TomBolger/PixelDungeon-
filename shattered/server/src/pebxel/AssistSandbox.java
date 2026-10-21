package pebxel;

import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.*;
import java.util.function.Supplier;

/**
 * Runs something that calls Assist.plan without leaving a trace in the Assistant's state between turns (its goal,
 * waits counted, the list read for "mind reading", the player's way...). AssistBot WATCH=1 asks the bot what it would
 * do (a good player's next click, the reference) after the watch has made its own list for the turn: a second plan
 * in a turn is not the same as none (a wait counted twice, the mind read against itself), so the bot's is undone.
 * Every static field of Assist is put back as it was, arrays and collections by content (in the same objects).
 */
final class AssistSandbox {
	private AssistSandbox() { }

	private static List<Field> fields;

	static <T> T run(Supplier<T> s) {
		if (fields == null) {
			fields = new ArrayList<>();
			for (Field f : Assist.class.getDeclaredFields()) {
				if (!Modifier.isStatic(f.getModifiers())) continue;
				boolean fin = Modifier.isFinal(f.getModifiers());
				Class<?> t = f.getType();
				if (fin && !(Collection.class.isAssignableFrom(t) || Map.class.isAssignableFrom(t) || t.isArray())) continue;   // (constants)
				f.setAccessible(true);
				fields.add(f);
			}
		}
		Object[] ref = new Object[fields.size()], copy = new Object[fields.size()];
		try {
			for (int i = 0; i < ref.length; i++) {
				Object v = fields.get(i).get(null);
				ref[i] = v;
				if (v == null) continue;
				if (v.getClass().isArray()) { int n = Array.getLength(v); Object c = Array.newInstance(v.getClass().getComponentType(), n); System.arraycopy(v, 0, c, 0, n); copy[i] = c; }
				else if (v instanceof Collection) copy[i] = new ArrayList<>((Collection<?>) v);
				else if (v instanceof Map) copy[i] = new LinkedHashMap<>((Map<?, ?>) v);
			}
		} catch (IllegalAccessException e) { throw new RuntimeException(e); }
		try { return s.get(); }
		finally {
			try {
				for (int i = 0; i < ref.length; i++) {
					Field f = fields.get(i); Object v = ref[i];
					if (!Modifier.isFinal(f.getModifiers())) f.set(null, v);
					if (v == null || copy[i] == null) continue;
					if (v.getClass().isArray()) System.arraycopy(copy[i], 0, v, 0, Array.getLength(v));
					else if (v instanceof Collection) { @SuppressWarnings("unchecked") Collection<Object> c = (Collection<Object>) v; c.clear(); c.addAll((Collection<?>) copy[i]); }
					else if (v instanceof Map) { @SuppressWarnings("unchecked") Map<Object, Object> m = (Map<Object, Object>) v; m.clear(); m.putAll((Map<?, ?>) copy[i]); }
				}
			} catch (IllegalAccessException e) { throw new RuntimeException(e); }
		}
	}
}
