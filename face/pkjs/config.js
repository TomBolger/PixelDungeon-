// Clay settings page for the Pixel Dungeon Face (phone side only; the normal app has no settings page)
var SLOT = [
  { label: 'The game\'s own number', value: '0' },
  { label: 'Time', value: '1' }, { label: 'Date', value: '2' },
  { label: 'Temperature now', value: '3' }, { label: 'Today\'s high', value: '4' }, { label: 'Today\'s low', value: '5' }, { label: 'High / low', value: '15' },
  { label: 'Steps today', value: '6' }, { label: 'Heart rate', value: '7' }, { label: 'Battery %', value: '8' },
  { label: 'Day of the week', value: '9' }, { label: 'Day of the month', value: '10' }, { label: 'Seconds (as of the last redraw)', value: '11' }, { label: 'AM / PM', value: '12' },
  { label: 'Gold', value: '13' }, { label: 'Health %', value: '14' }, { label: 'Empty', value: '16' }
];
function slot(key, label, def, desc) { return { type: 'select', messageKey: key, label: label, defaultValue: def || '0', options: SLOT, description: desc }; }
module.exports = [
  { type: 'heading', defaultValue: 'Pixel Dungeon Face' },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Controls' },
    { type: 'toggle', messageKey: 'CTL_NEW', label: 'Start a new run', defaultValue: false,
      description: 'Switch on and tap Save: the watch starts a new run right away (with the classes and starting stage below). It switches itself off again.' },
    { type: 'toggle', messageKey: 'CTL_TELE', label: 'Teleport the hero', defaultValue: false,
      description: 'Switch on and tap Save: the hero is moved to a random place on the floor, as a Scroll of Teleportation would (not on a boss floor). It switches itself off again.' }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Cheats' },
    { type: 'text', defaultValue: 'This game is hard, and the bot is not very good at it yet. Since this is for entertainment only, use these cheats to see deeper parts of the dungeon and spend less time resting!' },
    { type: 'toggle', messageKey: 'GOD', label: 'God mode', defaultValue: false,
      description: 'The hero cannot die: his health never falls below 1 (starving, falls, traps and explosions included).' },
    { type: 'select', messageKey: 'REGEN', label: 'Healing speed', defaultValue: '2', options: [
      { label: 'Normal', value: '0' }, { label: '2x', value: '1' }, { label: '3x', value: '2' }, { label: '5x', value: '3' } ],
      description: 'How fast health comes back by itself.' },
    { type: 'toggle', messageKey: 'HEAL', label: 'Ever-present healing potion', defaultValue: true,
      description: 'One Potion of Healing is always in the backpack (drunk, lost or stolen, it comes back). Off: only the potions he finds.' },
    { type: 'toggle', messageKey: 'UNBREAK', label: 'Unbreakable gear', defaultValue: true,
      description: 'Weapons and armour wear down but never break.' },
    { type: 'toggle', messageKey: 'LOOT', label: 'Better loot', defaultValue: true,
      description: 'Weapons and armour are found a little better than in the original game. Off: the original odds.' },
    { type: 'toggle', messageKey: 'HUNGER', label: 'Slow hunger', defaultValue: true,
      description: 'Hunger comes 8 times slower. Off: the original pace.' }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Starting stage' },
    { type: 'select', messageKey: 'STAGE', label: 'New runs start at', defaultValue: '0', options: [
      { label: 'Sewers (floor 1)', value: '0' }, { label: 'Goo (floor 5)', value: '1' }, { label: 'Prison (floor 6)', value: '2' },
      { label: 'Tengu (floor 10)', value: '3' }, { label: 'Caves (floor 11)', value: '4' }, { label: 'DM-300 (floor 15)', value: '5' },
      { label: 'Dwarven City (floor 16)', value: '6' }, { label: 'Dwarf King (floor 20)', value: '7' },
      { label: 'Demon Halls (floor 21)', value: '8' }, { label: 'Yog-Dzewa (floor 25)', value: '9' } ],
      description: 'Starting deeper, the hero arrives with a level, gear and a backpack of things (some potions and scrolls already known) like a player would have there. Applies to the next new run.' }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Classes' },
    { type: 'checkboxgroup', messageKey: 'CLASSES', label: 'New runs pick from', defaultValue: [true, true, true, true],
      options: ['Warrior', 'Mage', 'Rogue', 'Huntress'],
      description: 'A new run picks one of the ticked classes at random, never the same as the last run (unless only one is ticked). None ticked: all four.' }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'The big clock' },
    { type: 'toggle', messageKey: 'CLK_ON', label: 'Show the big clock', defaultValue: true },
    { type: 'select', messageKey: 'CLK_FMT', label: '12 or 24 hours', defaultValue: '0', options: [
      { label: 'Follow the watch', value: '0' }, { label: '12 hours', value: '1' }, { label: '24 hours', value: '2' } ] },
    { type: 'select', messageKey: 'CLK_SIZE', label: 'Size', defaultValue: '0', options: [ { label: 'Big', value: '0' }, { label: 'Smaller', value: '1' } ] },
    { type: 'toggle', messageKey: 'CLK_LINE', label: 'Outline and shadow', defaultValue: true }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'What each number shows' },
    slot('SL_DEPTH', 'Floor number'),
    slot('SL_KEYS', 'Keys held'),
    slot('SL_LVL', 'Hero level'),
    slot('SL_HP', 'Health'),
    slot('SL_GOLD', 'Gold in the backpack', '0', 'Seen when the bot opens its backpack.'),
    { type: 'select', messageKey: 'DATE_ORD', label: 'Date order', defaultValue: '0', options: [
      { label: 'Month / Day', value: '0' }, { label: 'Day / Month', value: '1' }, { label: 'Same as the phone', value: '2' } ],
      description: 'For any number set to Date.' }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Game speed' },
    { type: 'select', messageKey: 'SPEED', label: 'Speed of play', defaultValue: '0', options: [
      { label: 'Fast', value: '0' }, { label: 'Normal', value: '1' }, { label: 'Relaxed', value: '2' } ],
      description: 'How long the bot takes between moves, and over each screen it opens (backpack, items). Fast is the original pace. Normal is about 1.6 times as slow, Relaxed about 2.5 times (windows a little less). Fewer moves are played in each wake.' }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Battery saver' },
    { type: 'select', messageKey: 'BURST', label: 'Play per wake', defaultValue: '0', options: [
      { label: 'Long (20 seconds)', value: '0' }, { label: 'Medium (10 seconds)', value: '1' }, { label: 'Short (5 seconds)', value: '2' } ],
      description: 'How long the game plays each time you flick your wrist, tap, or the face appears. Then it freezes again.' },
    { type: 'select', messageKey: 'FPS', label: 'Frame rate', defaultValue: '0', options: [
      { label: 'Full (about 30 a second)', value: '0' }, { label: 'Reduced (about 15 a second)', value: '1' }, { label: 'Low (about 7 a second, no zoom-out glances)', value: '2' } ],
      description: 'How often the picture is redrawn while it plays. Lower uses less battery; the game itself plays the same.' }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Vibration' },
    { type: 'toggle', messageKey: 'VIBE', label: 'Vibrate for game events', defaultValue: false,
      description: 'A buzz when the hero is hurt or dies. Off by default.' }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Weather' },
    { type: 'select', messageKey: 'UNIT_F', label: 'Degrees', defaultValue: '1', options: [ { label: '°F', value: '1' }, { label: '°C', value: '0' } ],
      description: 'From Open-Meteo, using the phone\'s location, refreshed about every 45 minutes.' }
  ] },
  { type: 'submit', defaultValue: 'Save' }
];
