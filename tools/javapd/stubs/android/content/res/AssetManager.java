package android.content.res;
import java.io.*;
public class AssetManager {
	public InputStream open(String name) throws IOException { return new ByteArrayInputStream(new byte[0]); }
	public AssetFileDescriptor openFd(String name) throws IOException { throw new FileNotFoundException(name); }
}
