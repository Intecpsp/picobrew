import type { Recipe, ZProgram, ZProgramStep } from '../types'
import { newRecipe } from './utils'

export function activeZStep(
  name = 'Mash',
  location = 2,
  temp = 67,
  hold = 3600,
  drain = 0
): ZProgramStep {
  return { Name: name, StepType: 1, Location: location, Temp: temp, Time: hold, Drain: drain }
}

// The example follows the seven-step schedule embedded in the OEM Z Program Maker.
export function sampleZProgram(): ZProgram {
  return {
    Name: 'My Sample Recipe',
    IsMetric: true,
    IsBeer: true,
    UsePicoPakMode: false,
    Steps: [
      activeZStep('Heat Water', 1, 67.1, 0),
      activeZStep('Mash', 2, 67, 5400, 480),
      activeZStep('Heat to Boil', 1, 97, 0),
      activeZStep('Boil Adjunct 1', 3, 97, 2700),
      activeZStep('Boil Adjunct 2', 4, 97, 300),
      activeZStep('Boil Adjunct 3', 5, 97, 300),
      activeZStep('Boil Adjunct 4', 6, 97, 300, 300),
    ],
  }
}

export function newZRecipe(name = ''): Recipe {
  return {
    ...newRecipe(),
    name,
    batch_liters: 0,
    efficiency: 0,
    steps: [],
    kind: 'native-program',
    source_model: 'z',
    native_program: {
      id: '',
      name,
      steps: [],
      z_program: { Name: name, IsBeer: true, UsePicoPakMode: false, Steps: [activeZStep()] },
    },
  }
}

export function zDuration(seconds: number): string {
  const minutes = Math.floor(seconds / 60),
    remainder = seconds % 60
  return remainder
    ? minutes
      ? `${minutes} min ${remainder} s`
      : `${remainder} s`
    : `${minutes} min`
}
