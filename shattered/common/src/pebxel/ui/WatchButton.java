package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.watabou.noosa.Image;
import com.watabou.noosa.NinePatch;
import java.util.List;

/**
 * Pebble PD's red button: its chrome (58,0,6,6, margin 2), the label in Gothic 18 bold, white, centred (two lines
 * of Gothic 14 bold when it won't fit), and maybe a picture above the label.
 */
public class WatchButton extends Button {
	protected NinePatch bg;
	protected Image icon;
	protected String label = "";
	private final java.util.List<WatchText> lines = new java.util.ArrayList<>();
	public int labelColor = 0xFFFFFF;
	/** Where the label's box starts below the top (0: centred in the button). */
	public int labelTop = 0;
	/** Gothic 14 bold even when Gothic 18 would fit (Pebble PD's settings tiles). */
	public boolean small;

	public WatchButton(String label) {
		super();
		this.label = label == null ? "" : label;
		refreshLabel();
	}

	@Override
	protected void createChildren() {
		super.createChildren();
		bg = new NinePatch(PdArt.tex("chrome"), 58, 0, 6, 6, 2);
		add(bg);
	}

	public WatchButton icon(Image i) {
		if (icon != null) icon.killAndErase();
		icon = i;
		if (i != null) add(i);
		layout();
		return this;
	}

	public void label(String s) { label = s == null ? "" : s; refreshLabel(); }

	private void refreshLabel() {
		for (WatchText t : lines) t.killAndErase();
		lines.clear();
		if (label.isEmpty()) return;
		int w = Math.max(10, (int) width - 6);
		if (!small && (width == 0 || WatchText.width(WatchText.G18B, label) <= w)) lines.add(new WatchText(WatchText.G18B, label, labelColor));
		else if (small && WatchText.width(WatchText.G14B, label) > w + 4 && WatchText.width(WatchText.G14, label) <= w + 4)
			lines.add(new WatchText(WatchText.G14, label, labelColor));   // one line in the plain face rather than two over the picture
		else for (String s : WatchHud.wrap(label, WatchText.G14B, w)) lines.add(new WatchText(WatchText.G14B, s, labelColor));
		for (WatchText t : lines) add(t);
		layout();
	}

	@Override
	protected void layout() {
		super.layout();
		if (bg == null) return;
		bg.x = x; bg.y = y; bg.size(width, height);
		if (!small && lines.size() == 1 && lines.get(0).font == WatchText.G18B && WatchText.width(WatchText.G18B, label) > width - 6 && width > 0) { refreshLabel(); return; }
		int lh = lines.isEmpty() ? 0 : WatchWindow.lineHeight(lines.get(0).font);
		int total = lines.size() * lh;
		int top = labelTop > 0 ? (int) y + labelTop : icon != null ? (int) (y + height - total - (small ? 6 : 3)) : (int) (y + (height - total) / 2);
		for (int i = 0; i < lines.size(); i++) WatchHud.place(lines.get(i), (int) x, top + i * lh, (int) width, 1);
		if (icon != null) {
			icon.x = x + (width - icon.width()) / 2;
			icon.y = labelTop > 0 || lines.isEmpty() ? y + 3 : y + Math.max(3, (height - total - icon.height()) / 2 - 1);
		}
	}
}
