const defaultbeer = {
  "Name": "My Sample Recipe",
  "IsMetric": true,
  "IsBeer": true,
  "UsePicoPakMode": false,
  "Steps": [
    {
      "StepType": 1,
      "Name": "Heat Water",
      "Temp": 67.1,
      "Time": 0,
      "Drain": 0,
      "Location": 1
    },
    {
      "StepType": 1,
      "Name": "Mash",
      "Temp": 67,
      "Time": 5400,
      "Drain": 480,
      "Location": 2
    },
    {
      "StepType": 1,
      "Name": "Heat to Boil",
      "Temp": 97,
      "Time": 0,
      "Drain": 0,
      "Location": 1
    },
    {
      "StepType": 1,
      "Name": "Boil Adjunct 1",
      "Temp": 97,
      "Time": 2700,
      "Drain": 0,
      "Location": 3
    },
    {
      "StepType": 1,
      "Name": "Boil Adjunct 2",
      "Temp": 97,
      "Time": 300,
      "Drain": 0,
      "Location": 4
    },
    {
      "StepType": 1,
      "Name": "Boil Adjunct 3",
      "Temp": 97,
      "Time": 300,
      "Drain": 0,
      "Location": 5
    },
    {
      "StepType": 1,
      "Name": "Boil Adjunct 4",
      "Temp": 97,
      "Time": 300,
      "Drain": 300,
      "Location": 6
    }
  ]
}

let   step_contain = null
let   sCount   = 0
const sid          = 'id-s-'
const maxC         = 98.0

const step_props = {
  Name:     1,
  StepType: 2,
  Location: 3,
  Temp:     4,
  Time:     5,
  Drain:    6,
}

const loc = {
  Passthrough: 1,
  Mash:        2,
  Adjunct1:    3,
  Adjunct2:    4,
  Adjunct3:    5,
  Adjunct4:    6
}

const type = {
  ActiveStep: 1,
  PauseStep: 2,
}

const unit = {
  Metric: 0,
  Imperial: 1
}

function getId(id)
{
  return document.getElementById(id);
}

function getCallback(response)
{
  convertRecipe(JSON.parse(response.responseText), false)
}

function postCallback(response)
{
  if(response.status == 200) {
    displayAlertFor('z-conf', 'Recipe Pushed to Z!', 5)
  }
  else {
    displayAlertFor('z-error', 'An Error Has Occurred(' + response.responseText + ')', 4)
  }
  console.log('response:')
  console.log(response)
}

function getStep(n) {
  return getEl(sid + n)
}

function getStepPropElById(n, step_prop_id)
{
  return getId('s-' + n + '-' + step_prop_id )
}

function getStepProps(n)
{
  var prefix = 's-' + n + '-'
  return {
    "Name"     : getId(prefix + step_props.Name ),
    "StepType" : getId(prefix + step_props.StepType ),
    "Location" : getId(prefix + step_props.Location ),
    "Temp"     : getId(prefix + step_props.Temp     ),
    "Time"     : getId(prefix + step_props.Time     ),
    "Drain"    : getId(prefix + step_props.Drain    ),
  }
}

function getUnits() {
  return getEl('z-unit').selectedIndex
}

function maxTemp(metric) {
  return metric ? maxC : cToF(maxC)
}

function placeholderTempText(metric) {
  var u = metric ? 'C': 'F'
  return '°' + u
}

function placeholderTimeText(metric) {
  return 'Minutes'
}

function toggleDisableElement(el, disable)
{
  if (disable) {
    el.setAttribute("disabled", "")
    el.style.opacity = .2
  } else {
    el.removeAttribute("disabled", "")
    el.style.opacity = 1
  }
}

function updateType(n)
{
  var props = getStepProps(n)
  var pause = ( props.StepType.selectedIndex + 1 ) == type.PauseStep
  toggleDisableElement(props.Location, pause)
  toggleDisableElement(props.Temp, pause)
  toggleDisableElement(props.Drain, pause)
}

function convertUnits(toMetric) {
  var max    = maxTemp(toMetric)
  var place  = placeholderTempText(toMetric)

  for(var s = 1; s <= sCount; s++) {

    var temp = getStepPropElById(s, step_props.Temp)
    var t = parseFloat(temp.value)
    if(!Number.isNaN(t)) {
      temp.value = toMetric ? fToC(t) : cToF(t)
    }
    temp.placeholder = place
    temp.max         = max
  }
}

function updateUnits(u) {
  convertUnits(u == unit.Metric)
}

function setDefault()
{
  convertRecipe(defaultbeer, false)
}

function clearSteps()
{
  step_contain.innerHTML = ''
  sCount = 0
}

function convertRecipe(data, ignoreUnit)
{
  clearSteps()
  var units = getUnits()
  
  if(data.Name) { getEl('z-rec').value = data.Name }
  if(data.UsePicoPakMode) { getEl('z-pak').checked = true }
  if(data.Steps && data.Steps.length > 0) {
    for(var i in data.Steps)
    {
      var d = data.Steps[i]
      add()
      var props  = getStepProps(sCount)
      if(d.Name)     { props.Name.value = d.Name }
      if(d.StepType) { props.StepType.selectedIndex = (d.StepType - 1) }
      if(d.Location) { props.Location.selectedIndex = (d.Location - 1) }
      if(d.Temp)     { props.Temp.value = d.Temp }
      if(d.Time)     { props.Time.value = (d.Time/60) }
      if(d.Drain)    { props.Drain.value = (d.Drain/60) }

      updateType(sCount)
    }
  }
  if(!ignoreUnit && units == unit.Imperial) {
    convertUnits(false)
  }
}

function getOptions(collection) {
  var s = ''
  for (var opt in collection) {
    s += '<option value="' + collection[opt]  + '" label="' + opt + '">' + opt  + '</option>'
  }
  return s
}

function htmlSRowField(name, fieldHtml)
{
  return '<div class="s-row"><div class="w-50">' + name + '</div> ' + fieldHtml + '</div>'
}

function htmlstep(n)
{
  var el     = document.createElement("div")
  var metric = getUnits() == unit.Metric
  el.classList.add("step")
  el.id = sid + n
  el.innerHTML =
    '<h3 class="s-row title -num">' + n + '</h3>' +
    '<input id="s-' + n + '-1" class="w-100" type="text" placeholder="Step Name" maxlength="20">' +
    '<select id="s-' + n + '-2" onchange="updateType(' + n + ')">' +
    getOptions(type) +
    '</select>' +
    '<select id="s-' + n + '-3">' +
    getOptions(loc) +
    '</select>' +
    htmlSRowField('Temp',       '<input id="s-' + n + '-4" type="number" placeholder="' + placeholderTempText(metric) + '" step="0.01" min="-20" max="' + maxTemp(metric)  + '">') +
    htmlSRowField('Time',       '<input id="s-' + n + '-5" type="number" placeholder="Minutes" min="0" max="1440">') +
    htmlSRowField('Drain Time', '<input id="s-' + n + '-6" type="number" placeholder="Minutes" min="0" max="120">')
  return  el
}

function stepmodel(props)
{
  var s = {
    "Name": props.Name.value,
    "StepType": parseInt(props.StepType.value),
    "Time": props.Time.value * 60,
  } 
  if (s.StepType == type.ActiveStep) {
    s["Location"] = parseInt(props.Location.value)
    s["Temp"]     = parseFloat(props.Temp.value)
    s["Drain"]    = props.Drain.value * 60
    if(getUnits() == unit.Imperial && !Number.isNaN(s["Temp"]))
    {
      s["Temp"] = fToC(s["Temp"])
    }
  }
  return s
}

function add()
{
  sCount++
  var s = htmlstep(sCount)
  if(step_contain) {
    step_contain.appendChild(s)
  }
}

function remove()
{
  if(sCount > 1)
  {
    step_contain.removeChild(getStep(sCount))
    sCount--
  }
}

function getProgramAsJson()
{
  var program = {
    "IsBeer": true,
    "Name": getEl('z-rec').value,
    "UsePicoPakMode": getEl('z-pak').checked,
    "Steps": []
  }

  for (var i = 1; i <= sCount; i++)
  {
    program.Steps.push(stepmodel(getStepProps(i)))
  }

  return program;
}

function pushToZ()
{
  var pgm = getProgramAsJson()
  post(pgm, 'recipe', postCallback)
}

function download()
{
  var pgm = getProgramAsJson()
  var json = JSON.stringify(pgm, null, '  ');
  var blob = new Blob([json], {type: "application/json"});

  // Generate a temporay link to trigger download
  var link = document.createElement('a')
  link.href = URL.createObjectURL(blob)
  link.download = "program.json"
  link.click()
}

function displayAlertFor(id, alertText, sec) {
  var el = getEl(id)
  el.innerHTML = alertText
  el.style.display = "block"

  setTimeout(function() {
    el.style.display = "none"
  }, (sec * 1000) )
}

function onReaderLoad(event){
  try {
    var data = JSON.parse(event.target.result);
    console.log(data)
  }
  catch (e) {
    displayAlertFor('z-error', 'cannot parse JSON(' + e + ')', 5)
  }
  clearSteps()
  convertRecipe(data, false)
}

function fileUpload(file, loadCallback) {
  var reader = new FileReader();
  reader.onload = loadCallback;
  reader.readAsText(file);
}

function handleInputFileEvent(e) {
  fileUpload(e.target.files[0], onReaderLoad)
}

function stopDefaults(e) {
  e.preventDefault()
  e.stopPropagation()
}

//drop listeners
function dropHandler(e)
{
  stopDefaults(e)
  fileUpload(e.dataTransfer.files[0], onReaderLoad)
  e.target.classList.remove('highlight')
}

function enterHandler(e) {
  stopDefaults(e)
  e.target.classList.add('highlight')
}

function leaveHandler(e) {
  stopDefaults(e)
  if(e.target.classList && e.target.classList.contains('highlight'))
    e.target.classList.remove('highlight')
}

function setup()
{
  step_contain = getEl('step-contain')
  add()
  
  //file drop
  var drop_area = getEl('drop-area')
  drop_area.addEventListener('drop', dropHandler, false)
  drop_area.addEventListener('dragover',  enterHandler, false)
  drop_area.addEventListener('dragleave', leaveHandler, false)
}

document.addEventListener('DOMContentLoaded', setup())
