package com.watabou.noosa;

import java.util.ArrayList;

/** Lets the watch bridge read a UI group's children (SPD keeps the list protected). */
public final class GroupAccess {
	public static synchronized ArrayList<Gizmo> members(Group g) { return new ArrayList<>(g.members); }
}
