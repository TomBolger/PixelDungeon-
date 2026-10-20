package pebxel.ui;

import com.watabou.noosa.ui.Component;

/** A window that moves its own button focus (Pebble PD's menu: Up/Down walk every page's buttons in turn). */
public interface Navigable {
	/** Up (-1) or Down (+1). */
	void nav(int dir);
	/** The button Select presses (Controls frames it). */
	Component focused();
}
