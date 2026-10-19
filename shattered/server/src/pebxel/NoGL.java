package pebxel;

import com.badlogic.gdx.graphics.GL20;
import com.badlogic.gdx.graphics.GL30;
import java.lang.reflect.Proxy;
import java.nio.IntBuffer;

/** A graphics card that does nothing: every OpenGL call succeeds and draws nothing. */
public final class NoGL {
	private static int ids = 1;

	public static GL20 make() {
		return (GL20) Proxy.newProxyInstance(NoGL.class.getClassLoader(), new Class[]{GL20.class, GL30.class},
				(p, m, a) -> {
					String n = m.getName();
					switch (n) {
						case "glGetShaderiv": case "glGetProgramiv":
							((IntBuffer) a[2]).put(((IntBuffer) a[2]).position(), 1); return null;
						case "glGetIntegerv":
							((IntBuffer) a[1]).put(((IntBuffer) a[1]).position(), 4096); return null;
						case "glGetString": case "glGetShaderInfoLog": case "glGetProgramInfoLog": return "";
						case "glCheckFramebufferStatus": return GL20.GL_FRAMEBUFFER_COMPLETE;
						case "hashCode": return System.identityHashCode(p);
						case "equals": return p == a[0];
						case "toString": return "NoGL";
					}
					Class<?> r = m.getReturnType();
					if (r == int.class) return n.startsWith("glGen") || n.startsWith("glCreate") ? ids++ : 0;
					if (r == boolean.class) return false;
					if (r == float.class) return 0f;
					if (r == long.class) return 0L;
					return null;
				});
	}
}
