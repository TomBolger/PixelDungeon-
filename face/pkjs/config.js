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
