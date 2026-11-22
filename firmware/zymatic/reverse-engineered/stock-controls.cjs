'use strict';

// A behavioral reconstruction of selected stock 1.1.14 operations. Logical
// inputs are sampled values. These functions never drive a physical output.
function armStopIndex(location) {
  return [0, 1, 3, 4, 5, 6][location] ?? 0;
}

function filterRemoved(firstRead, afterFiveMilliseconds) {
  return !firstRead && !afterFiveMilliseconds;
}

function outputFault(pump, heater, backupRelay) {
  if (!pump) return 9;
  return heater && !backupRelay ? 17 : 0;
}

function heatDifferenceFault(enabled, wort, primary, backup) {
  return enabled && [primary, backup].some(channel =>
    channel.every((sample, i) => sample - wort[i] >= 50));
}

function heatRiseFault(enabled, primary, backup) {
  // 0x38d6 reads the current primary heat channel for the final backup
  // comparison. Preserve this observed OEM behavior.
  const backupComparisons = [backup[0], backup[1], backup[2], primary[3]];
  return enabled && [primary, backupComparisons].some(channel =>
    channel.slice(1).every((sample, i) => sample - channel[i] >= 28));
}

function absoluteHeatCutout(wort, primary) {
  return wort > 214 || primary > 225;
}

module.exports = {armStopIndex, filterRemoved, outputFault, heatDifferenceFault, heatRiseFault, absoluteHeatCutout};
