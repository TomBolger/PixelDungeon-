package sim;

import com.watabou.pixeldungeon.levels.Level;
import com.watabou.pixeldungeon.levels.Terrain;

/**
 * A hand-made level. The layout is ASCII art pasted at (OX, OY) of the 32x32 map;
 * everything outside it is WALL.
 *
 *   #  wall          .  floor (EMPTY)     +  door
 *   "  grass         G  high grass        ~  water
 *   E  embers        @  marks the hero's cell; its terrain is heroTile (default '.')
 *   a..z  floor, marks cells for mobs / blobs (looked up with cell('a'))
 */
public class TestLevel extends Level {

	public static final int OX = 8, OY = 8;

	private final String[] art;
	private final char heroTile;

	public TestLevel( String... art ) {
		this( '.', art );
	}

	/** heroTile: terrain char (from the legend above) placed under the '@' marker. */
	public TestLevel( char heroTile, String... art ) {
		this.art = art;
		this.heroTile = heroTile;
		color1 = 0x48763c;
		color2 = 0x59994a;
	}

	/** Map cell of the first occurrence of marker c in the art, or -1. */
	public int cell( char c ) {
		for (int y = 0; y < art.length; y++) {
			int x = art[y].indexOf( c );
			if (x >= 0) return (OY + y) * WIDTH + (OX + x);
		}
		return -1;
	}

	@Override
	protected boolean build() {
		for (int i = 0; i < LENGTH; i++) map[i] = Terrain.WALL;
		for (int y = 0; y < art.length; y++) {
			String row = art[y];
			for (int x = 0; x < row.length(); x++) {
				int t;
				char ch = row.charAt( x );
				if (ch == '@') ch = heroTile;
				switch (ch) {
				case '#': t = Terrain.WALL; break;
				case '+': t = Terrain.DOOR; break;
				case '"': t = Terrain.GRASS; break;
				case 'G': t = Terrain.HIGH_GRASS; break;
				case '~': t = Terrain.WATER; break;
				case 'E': t = Terrain.EMBERS; break;
				case ' ': t = Terrain.WALL; break;
				default:  t = Terrain.EMPTY; break;
				}
				map[(OY + y) * WIDTH + (OX + x)] = t;
			}
		}
		entrance = exit = -1;
		int h = cell( '@' );
		entrance = h;
		exit = h;
		return true;
	}

	@Override protected void decorate() {}
	@Override protected void createMobs() {}
	@Override protected void createItems() {}

	@Override
	public String tilesTex() { return com.watabou.pixeldungeon.Assets.TILES_SEWERS; }
	@Override
	public String waterTex() { return com.watabou.pixeldungeon.Assets.WATER_SEWERS; }
}
