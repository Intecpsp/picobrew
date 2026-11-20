function post(data, url, callback) {
  var xhr = new XMLHttpRequest();

  xhr.addEventListener("readystatechange", function() {
    if(this.readyState === 4) {
      callback(this)
    }
  });

  xhr.open("POST", url, true);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.send(JSON.stringify(data));
}

function get(url, callback) {
  var xhr = new XMLHttpRequest();

  xhr.onreadystatechange = function () {
      if (this.readyState != 4) return;

      if (this.status == 200) {
          callback(this);
      }
  };

  xhr.open('GET', url, true);
  xhr.send();
}

function downloadObjectAsJson(data, filename){
  var dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(data));
  var downloadAnchorNode = document.createElement('a');
  downloadAnchorNode.setAttribute("href",     dataStr);
  downloadAnchorNode.setAttribute("download", filename + ".json");
  document.body.appendChild(downloadAnchorNode); // required for firefox
  downloadAnchorNode.click();
  downloadAnchorNode.remove();
}

function fToC(F) {
  return (F - 32) * 5 / 9
}

function cToF(C) {
  return C * 9 / 5 + 32
}

function getEl(id) {
  return document.getElementById(id)
}
