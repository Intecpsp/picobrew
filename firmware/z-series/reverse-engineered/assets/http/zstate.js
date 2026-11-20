let datael   = null
let jsondata = []
let interval = 5
let timer    = null

function getActiveStatus()
{
  var el = getEl('id-active')
  return el && el.checked
}

function updateGetCallback (response)
{
  jsondata.push(JSON.parse(response.responseText))
  datael.innerHTML = JSON.stringify(jsondata)
}

function updateStep()
{
  get('state', updateGetCallback)
  //get('https://jsonplaceholder.typicode.com/todos/1', updateGetCallback)
}

function updateTimer()
{
  clearInterval(timer);
  timer = setInterval(updateStep, interval * 1000);
}

function handleDownloadRequest()
{
  console.log('downloading data...')
  downloadObjectAsJson(jsondata, 'zdata')
}

function handleActiveChange(e)
{
  if(e.target.checked) {
    updateTimer()
  } else {
    clearInterval(timer)
  }
}

function handleIntervalChange(e)
{
  if(Number.isNaN(e.target.valueAsNumber)) { return; }

  interval = e.target.valueAsNumber

  if(getActiveStatus()) {
    updateTimer()
  }
}

function setup()
{
  datael = getEl('z-state-json')
  timer = setInterval(updateStep, interval * 1000)
}

document.addEventListener('DOMContentLoaded', setup())