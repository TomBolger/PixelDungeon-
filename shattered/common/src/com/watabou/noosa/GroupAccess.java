package com.watabou.noosa;

import java.util.ArrayList;

/** Lets the watch bridge read a UI group's children (SPD keeps the list protected). */
public final class GroupAccess {
	public static synchronized ArrayList<Gizmo> members(Group g) { return new ArrayList<>(g.members); }

	/** Put g into the group just after another member (drawn right above it). */
	public static void addAfter(Group grp, Gizmo g, Gizmo after) {
		synchronized (grp) {
			if (g.parent != null) g.parent.remove(g);
			int i = grp.members.indexOf(after);
			if (i < 0) { grp.add(g); return; }
			grp.members.add(i + 1, g);
			g.parent = grp;
			grp.length++;
		}
	}
}
