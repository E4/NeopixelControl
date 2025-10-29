'use strict';

/**
 * @constructor
 */
var Dyna = function(){
};

Dyna.BlockPropertyChange = new Set(["tagName", "element", "children"]);

Dyna.ActionKeys = ["Space","Enter"];

Dyna.create = function(dynaElement,parentHolder=null){
  if(!Array.isArray(dynaElement)) return Dyna.createElement(dynaElement,parentHolder);
  for(let i=0;i<dynaElement.length;i++) {
    Dyna.createElement(dynaElement[i],parentHolder,i);
  }
  return dynaElement;
}


Dyna.dynaSetterGetter = function(o,k) {
  var stored = o[k];
  var setFunction = function(newValue) {
    if(newValue==stored) return stored;
    stored=newValue;
    Dyna.update(o);
    return stored;
  }

  var setBlock = function(newValue) {
    return stored;
  }

  var getFunction = function() {
    return stored;
  }

  return {
    set: Dyna.BlockPropertyChange.has(k)?setBlock:setFunction,
    get: getFunction
  };
}

/*
  given a dyna element
  create an element and associated sub-elements
*/
Dyna.createElement = function(dynaElement,parentHolder,index=null) {
  if(dynaElement==null) return dynaElement;
  if(dynaElement.children==null) dynaElement.children=[];
  if(!dynaElement.element) {
    dynaElement.element = document.createElement(dynaElement.tagName);
    for(let key in dynaElement) {
      Object.defineProperty(dynaElement,key, Dyna.dynaSetterGetter(dynaElement,key));
    };
    if(parentHolder) {
      Dyna.appendChildToParentAtPosition(dynaElement.element,parentHolder,index);
    }
  }
  if(parentHolder && dynaElement.element.parentElement!=parentHolder) {
    Dyna.appendChildToParentAtPosition(dynaElement.element,parentHolder,index);
  } else if(parentHolder && index!=null && parentHolder.children[index] != dynaElement.element) {
    Dyna.appendChildToParentAtPosition(dynaElement.element,parentHolder,index);
  }
  if(dynaElement.children==null) dynaElement.children = [];
  var eventListeners = {};
  for(let property in dynaElement) {
    if(Dyna.BlockPropertyChange.has(property)) continue;
    if(property=="text") {
      Dyna.updateInnerText(dynaElement.element,dynaElement["text"]);
      continue;
    }
    if(property=="class") {
      if(dynaElement.element["className"]==dynaElement["class"]) continue;
      dynaElement.element["className"]=dynaElement["class"];
      continue;
    }
    if(property.startsWith("on-")) {
      eventListeners[property.substring(3)] = dynaElement[property];
      continue;
    }
    if(dynaElement.element[property]==dynaElement[property]) continue;
    dynaElement.element[property]=dynaElement[property];
  }
  Dyna.updateEventListeners(dynaElement.element, eventListeners);
  Dyna.updateChildren(dynaElement);
  return dynaElement;
}


/*
  given a child, parent and an index
  appends the child into the parent at the given index
  (it's like node.appendChild but not stupid)
*/
Dyna.appendChildToParentAtPosition = function(childElement,parentElement,insertPosition=null) {
  if(insertPosition==null) return parentElement.appendChild(childElement);
  if(insertPosition>=parentElement.children.length) return parentElement.appendChild(childElement);
  parentElement.insertBefore(childElement,parentElement.children[insertPosition]);
}


/*
  given an element and text
  update innerText of the element
*/
Dyna.updateInnerText = function(element, newText) {
  if(element.childNodes.length>0) {
    for(let i=0;i<element.childNodes.length;i++) {
      if(element.childNodes[i].nodeType==3) {
        if(element.childNodes[i].textContent != newText)
          element.childNodes[i].textContent = newText;
        return;
      }
    }
  } else if(element.innerText != newText) {
    element.innerText = newText;
  }
}


/*
  given a dyna element
  updates the children
*/
Dyna.updateChildren = function(dynaElement) {
  var dynaChildren = dynaElement.element.dynaChildren;
  var spliced;
  if(!dynaChildren) dynaElement.element.dynaChildren = dynaChildren = [];
  if("children" in dynaElement) {
    for(let i in dynaElement["children"]) {
      Dyna.createElement(dynaElement["children"][i],dynaElement.element,i);
      if(dynaChildren.indexOf(dynaElement["children"][i])==-1) dynaChildren.push(dynaElement["children"][i]);
    }
  }
  for(let i=dynaChildren.length-1;i>=0;i--) {
    if(dynaElement.children.indexOf(dynaChildren[i])!=-1) continue;
    if(spliced = dynaChildren.splice(i,1)[0]) spliced.element.remove();
  }
}


/*
  given an element and a structure of event listeners
  updates the element's even listeners if needed
*/
Dyna.updateEventListeners = function(element, newEventListeners) {
  var existingEventListeners = element.dynaEventListeners;
  if(!existingEventListeners) element.dynaEventListeners = existingEventListeners = {};
  // remove listeners
  for(let e in existingEventListeners) {
    if(e in newEventListeners && existingEventListeners[e]==newEventListeners[e]) continue;
    if(!(e in newEventListeners) || existingEventListeners[e]!=newEventListeners[e]) {
      element.removeEventListener(e,existingEventListeners[e]);
      delete existingEventListeners[e];
      continue;
    }
  }
  // add listeners
  for(let e in newEventListeners) {
    if(e in existingEventListeners) continue;
    element.addEventListener(e,newEventListeners[e]);
    existingEventListeners[e] = newEventListeners[e];
  }
}

Dyna.genericTag = function(tagname, cls, text, options, kids) {
  var rv = {"tagName":tagname, "class":cls, "text":text, "children": kids};
  for(let o in options) rv[o] = options[o];
  return rv;
}

Dyna.div   = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("div",    cls, text, options, kids);
Dyna.h1    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("h1",     cls, text, options, kids);
Dyna.h2    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("h2",     cls, text, options, kids);
Dyna.h3    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("h3",     cls, text, options, kids);
Dyna.span  = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("span",   cls, text, options, kids);
Dyna.p     = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("p",      cls, text, options, kids);
Dyna.a     = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("a",      cls, text, options, kids);
Dyna.img   = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("img",    cls, text, options, kids);
Dyna.form  = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("form",   cls, text, options, kids);
Dyna.table = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("table",  cls, text, options, kids);
Dyna.tr    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("tr",     cls, text, options, kids);
Dyna.td    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("td",     cls, text, options, kids);
Dyna.th    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("th",     cls, text, options, kids);
Dyna.a     = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("a",      cls, text, options, kids);
Dyna.ul    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("ul",     cls, text, options, kids);
Dyna.ol    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("ol",     cls, text, options, kids);
Dyna.li    = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("li",     cls, text, options, kids);
Dyna.i     = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("i",      cls, text, options, kids);
Dyna.butt  = (cls,text=null,options=null,kids=[])=>Dyna.genericTag("button", cls, text, options, kids);

Dyna.ahref = (cls,text,href,options=null,kids=[])=>{
  if(options== null) options = {};
  if(options.href==null) options.href = href;
  return Dyna.genericTag("a", cls, text, options, kids);
}

Dyna.img = (cls,src,options=null,kids=[])=>{
  if(options==null) options = {};
  if(options.src==null) options.src = src;
  return Dyna.genericTag("img", cls, null, options, kids);
}

Dyna.btnLbl = function(cls,text,options=null,kids=[]) {
  var rv = {
    "tagName":"label",
    "class":cls,
    "text":text,
    "tabIndex":0,
    "on-keypress": (e)=>{if(Dyna.ActionKeys.indexOf(e.code)!=-1) e.target.click();},
    "on-mouseout": (e)=>{e.target.blur();},
    "children": kids
  }
  for(let o in options) rv[o] = options[o];
  return rv;
}

Dyna.btn = function(cls,text,options=null,kids=[]) {
  var rv = {
    "tagName":"button",
    "class":cls,
    "text":text,
    "tabIndex":0,
    "on-keypress": (e)=>{if(Dyna.ActionKeys.indexOf(e.code)!=-1) e.target.click();},
    "on-mouseout": (e)=>{e.target.blur();},
    "children": kids
  }
  for(let o in options) rv[o] = options[o];
  return rv;
}

Dyna.input = function(cls, type, options, kids=[]) {
  const noop = ()=>{};
  var rv = {
    "tagName":"input",
    "class":cls,
    "type":type,
    "children": kids
  }
  for(let o in options) rv[o] = options[o];
  var requestedOnChange = rv["on-change"];
  rv["on-change"] = (e)=>{rv.value = e.target.value;if(rv.checked!=undefined) rv.checked=e.target.checked;requestedOnChange?requestedOnChange(e):noop()};
  return rv;
}

Dyna.textarea = function(cls, options, kids) {
  var rv = {
    "tagName": "textarea",
    "class": cls,
    "children": kids
  };
  for (var o in options) rv[o] = options[o];
  return rv;
}

Dyna.select = function(cls, options, kids) {
  var rv = {
    "tagName":"select",
    "class":cls,
    "children": kids
  }
  for(let o in options) rv[o] = options[o];
  return rv;
}

Dyna.label = function(cls, text, options, kids) {
  var rv = {
    "tagName":"label",
    "class":cls,
    "text":text,
    "children": kids
  }
  for(let o in options) rv[o] = options[o];
  return rv;
}

Dyna.options = function(optionlist, defaultOption = false, defaultText="") {
  var rv = [];
  if (defaultOption) {
    rv.push({ "tagName": "option", "value": "", "disabled": true, "selected": true, "style": "display:none;", "text": defaultText  });
  }
  for (var optionitem in optionlist) {
    var option = optionlist[optionitem];
    if (typeof option !== 'object') {
      rv.push({ "tagName": "option", "value": option, "text": option });
    } else {
      rv.push({ "tagName": "option", "value": option.value, "text": option.text });
    }
  }
  return rv;
}


Dyna.update = function(e) {
  Dyna.create(e,null);
}

Dyna.updateElement = function(e) {
  Dyna.createElement(e,null);
}

Dyna.show = (e)=>{
  e["class"] = e["class"].split(" ").filter(s=>s!=="hide").join(" ");
}
Dyna.hide = (e)=>{
  var c=e["class"].split(" ");
  if(!c.includes("hide")) c.push('hide'); e["class"]=c.join(" ");
}
Dyna.toggle = (e)=>{
  var c=e["class"].split(" ");
  if(!c.includes("hide")) {
     c.push('hide'); e["class"]=c.join(" ");
  } else {
    Dyna.show(e);
  }
}

Dyna.antishow = (e)=>{
  e["class"] = e["class"].split(" ").filter(s=>s!=="show").join(" ");
}
Dyna.antihide = (e)=>{
  var c=e["class"].split(" ");
  if(!c.includes("show")) c.push('show'); e["class"]=c.join(" ");
}
Dyna.antitoggle = (e)=>{
  var c=e["class"].split(" ");
  if(!c.includes("show")) {
     c.push('show'); e["class"]=c.join(" ");
  } else {
    Dyna.antishow(e);
  }
}

Dyna.replaceLast = (parentElem, elem) => {
  parentElem.children.pop();
  parentElem.children.push(elem);
  Dyna.update(parentElem);
}

/*
  given a dyna tree (like the one passed to createElement)
  removes the elements from the DOM
*/
Dyna.removeElement = function(dynaroot) {
  for(let i=0;i<dynaroot.length;i++) {
    if(!dynaroot[i].element) continue;
    dynaroot[i].element.remove();
  }
}


/**
 * @constructor
 */
var ChaserControl = function() {
  const uint16_t = ["v","number",{"min":"0","max":"65535","value":"0"}];
  const uint8_t = ["v","number",{"min":"0","max":"255","value":"0"}];
  const int8_t = ["v","number",{"min":"-128","max":"127","value":"0"}];
  const color_t = ["v","color",{"value":"#000000"}];

  // Posts raw chaser bytes given as a plain JS array (length always multiple of 32).
  async function postChaserBytes(arr) {
    const bytes = new Uint8Array(arr);
  
    const res = await fetch("/", {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: bytes,
    });
  
    return res;
  }
  
  function parseChasers(blob) {
    let buf;
    if (blob instanceof ArrayBuffer) buf = blob;
    else if (blob instanceof Uint8Array) buf = blob.buffer.slice(blob.byteOffset, blob.byteOffset + blob.byteLength);
    return buf;
  }

  async function fetchChasers() {
    const res = await fetch("/bin");
    if (!res.ok) {
      return null;
    }
    const buf = await res.arrayBuffer();
    if (buf.byteLength === 0) return [];
    return parseChasers(buf);
  }

  let fieldContainerChildren;
  let fieldContainer = Dyna.div("",null,null, fieldContainerChildren = []);
  let outerContainer = Dyna.div("",null,null, [fieldContainer,Dyna.butt("a","add",{"on-click":addAnotherChaser})])

  function addAnotherChaser() {
    appendChasers(new ArrayBuffer(32));
    gatherAndSendValues();
  }
  
  function appendChasers(dataArray) {
    if(!dataArray || !dataArray.byteLength) return;
    const dataView = new DataView(dataArray);
    const count = dataArray.byteLength / 32;
    for(let i=0;i<count;i++) {
      appendChaser(dataView,i*32);
    }
  }

  function appendChaser(dataView,base) {
    function removeThese() {
      let i = fieldContainerChildren.indexOf(chaserEntry);
      if(i!=-1) fieldContainerChildren.splice(i,1);
      Dyna.update(fieldContainer);
      gatherAndSendValues();
    }
    let u8 = (i)=>dataView.getUint8(base+i);
    let u16 = (i)=>dataView.getUint16(base+i,true);
    let i8 = (i)=>dataView.getInt8(base+i);
    let chaserEntry = Dyna.div("h","",null,[
      getUpdatedColorField(u8(0),u8(1),u8(2)),
      getUpdatedColorField(u8(3),u8(4),u8(5)),
      getUpdatedColorField(u8(6),u8(7),u8(8)),
      getUpdatedColorField(u8(9),u8(10),u8(11)),
      getUpdatedColorField(u8(12),u8(13),u8(14)),
      getUpdatedColorField(u8(15),u8(16),u8(17)),
      getGenericNumberField('Position Offset', uint16_t, u16(18)),
      getGenericNumberField('Position Skip', int8_t, i8(20)),
      getGenericNumberField('Position Delay', uint8_t, u8(21)),
      getGenericNumberField('Color Offset', uint8_t, u8(22)),
      getGenericNumberField('Color Skip', uint8_t, u8(23)),
      getGenericNumberField('Color Delay', uint8_t, u8(24)),
      getGenericNumberField('Repeat', uint8_t, u8(25)),
      getGenericNumberField('Range Length', uint16_t, u16(26)),
      getGenericNumberField('Range Offset', uint16_t, u16(28)),
      ...getFlagCheckboxes(u8(30)),
      Dyna.butt("r","remove",{"on-click":removeThese})
    ]);
    fieldContainerChildren.push(chaserEntry);
    Dyna.update(fieldContainer);
  }

  function getFlagCheckboxes(flagValues) {
    console.log(flagValues & 1);
    return [
      inputCheckbox(  "Clear Previous",   flagValues & 1),
      inputCheckbox( "Random Position",   flagValues & 2),
      inputCheckbox(    "Random Color",   flagValues & 4),
      inputCheckbox(  "Delayed Update",   flagValues & 8),
      inputCheckbox(      "Sinusoidal",  flagValues & 16),
      inputCheckbox(                "",  flagValues & 32),
      inputCheckbox(                "",  flagValues & 64),
      inputCheckbox(                "", flagValues & 128),
    ];
  }

  function inputCheckbox(labelText,boxIsChecked) {
    return Dyna.label("l",labelText,"",[Dyna.input("x","checkbox",{"on-change":gatherAndSendValues,"checked":!!boxIsChecked})]);
  }

  function getGenericNumberField(fieldLabel, fieldType, value) {
    var rv = [...fieldType];
    rv[2]["value"] = value.toString();
    rv[2]["on-change"] = gatherAndSendValues;
    return Dyna.label("n",fieldLabel,null,[Dyna.input(...rv)]);
  }

  function getUpdatedColorField(r,g,b) {
    var rv = [...color_t];
    rv[2]["value"] = "#"+toHex2(r)+toHex2(g)+toHex2(b);
    rv[2]["on-change"] = gatherAndSendValues;
    return Dyna.label("c",null,null,[Dyna.input(...rv)]);
  }

  function toHex2(n) {
    return n.toString(16).padStart(2, "0");
  }

  function gatherAndSendValues() {
    console.log("sending")
    var arrayBuffer = new ArrayBuffer(fieldContainerChildren.length*32);
    var dataView = new DataView(arrayBuffer);
    for(let i=0;i<fieldContainerChildren.length;i++) {
      gatherChaserValues(fieldContainerChildren[i]["children"],dataView,i*32);
    }
    postChaserBytes(arrayBuffer);
  }

  function gatherChaserValues(chaserElement, dataView, baseOffset) {
    gatherColorValue(chaserElement[0], dataView, baseOffset+0);
    gatherColorValue(chaserElement[1], dataView, baseOffset+3);
    gatherColorValue(chaserElement[2], dataView, baseOffset+6);
    gatherColorValue(chaserElement[3], dataView, baseOffset+9);
    gatherColorValue(chaserElement[4], dataView, baseOffset+12);
    gatherColorValue(chaserElement[5], dataView, baseOffset+15);
    dataView.setUint16(baseOffset+18,getValue(chaserElement[6]),true);
    dataView.setInt8(baseOffset+20,getValue(chaserElement[7]));
    dataView.setUint8(baseOffset+21,getValue(chaserElement[8]));
    dataView.setUint8(baseOffset+22,getValue(chaserElement[9]));
    dataView.setUint8(baseOffset+23,getValue(chaserElement[10]));
    dataView.setUint8(baseOffset+24,getValue(chaserElement[11]));
    dataView.setUint8(baseOffset+25,getValue(chaserElement[12]));
    dataView.setUint16(baseOffset+26,getValue(chaserElement[13]),true);
    dataView.setUint16(baseOffset+28,getValue(chaserElement[14]),true);
    dataView.setUint8(baseOffset+30,getFlagValues(chaserElement));
  }

  function gatherColorValue(colorElement, dataView, positionOffset) {
    let v = getValue(colorElement);
    dataView.setUint8(positionOffset+0, parseInt(v.slice(1, 3), 16)); // R
    dataView.setUint8(positionOffset+1, parseInt(v.slice(3, 5), 16)); // G
    dataView.setUint8(positionOffset+2, parseInt(v.slice(5, 7), 16)); // B
  }

  function getValue(labeledElement) {
    if(!labeledElement["children"].length) return null;
    return labeledElement["children"][0].value;
  }

  function getFlagValues(chaserElements) {
    return (
      chaserElements[15]["children"][0].checked*1+
      chaserElements[16]["children"][0].checked*2+
      chaserElements[17]["children"][0].checked*4+
      chaserElements[18]["children"][0].checked*8+
      chaserElements[19]["children"][0].checked*16+
      chaserElements[20]["children"][0].checked*32+
      chaserElements[21]["children"][0].checked*64+
      chaserElements[22]["children"][0].checked*128
    );
  }

  Dyna.create(outerContainer,document.body);
  fetchChasers().then(appendChasers);
}

window.addEventListener("load",()=>new ChaserControl());
