package pebxel.ui;

/** A watch control that shows its own keyboard focus (Pebble PD's underline or yellow box) instead of Controls' frame. */
public interface Focusable {
	void focus(boolean on);
}
