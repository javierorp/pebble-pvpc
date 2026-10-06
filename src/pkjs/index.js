'use strict';

var API_URL = 'https://apidatos.ree.es/es/datos/mercados/precios-mercados-tiempo-real';
var ZONAS = ['peninsular', 'canarias', 'baleares', 'ceuta', 'melilla'];
var NOMBRES_ZONA = ['Península', 'Canarias', 'Baleares', 'Ceuta', 'Melilla'];
var JS_READY = 2;
var SIN_DATO = 2147483647;
var KEY = {
  REQ: 10000,
  PRICES: 10001,
  HORA: 10002,
  MINH: 10003,
  MAXH: 10004,
  ERR: 10005,
  GEO: 10006,
  UNIDAD: 10007
};
var CLAVE_AJUSTES = 'pvpc_ajustes';

var PAGINA_CONFIG = '<!DOCTYPE html>' +
  '<html lang="es"><head><meta charset="utf-8">' +
  '<meta name="viewport" content="width=device-width, initial-scale=1">' +
  '<title>Precio PVPC - Ajustes</title><style>' +
  'body{font-family:sans-serif;background:#f2f2f2;margin:0;padding:16px;max-width:420px;margin:auto;color:#111}' +
  'h1{font-size:1.25em}' +
  'fieldset{border:1px solid #ccc;border-radius:8px;margin-bottom:16px;background:#fff}' +
  'legend{font-weight:bold;padding:0 6px}' +
  'select{width:100%;padding:10px;font-size:1.05em;border:1px solid #ccc;border-radius:6px;background:#fff;margin:8px 0}' +
  'button{width:100%;padding:12px;font-size:1.1em;background:#356aaa;color:#fff;border:0;border-radius:8px}' +
  '</style></head><body>' +
  '<h1>Precio PVPC - Ajustes</h1>' +
  '<form>' +
  '<fieldset><legend>Zona</legend>' +
  '<select name="zona">' +
  '<option value="0" __Z0__>Península</option>' +
  '<option value="1" __Z1__>Canarias</option>' +
  '<option value="2" __Z2__>Baleares</option>' +
  '<option value="3" __Z3__>Ceuta</option>' +
  '<option value="4" __Z4__>Melilla</option>' +
  '</select>' +
  '</fieldset>' +
  '<fieldset><legend>Unidad del precio</legend>' +
  '<select name="unidad">' +
  '<option value="0" __U0__>€/kWh</option>' +
  '<option value="1" __U1__>€/MWh</option>' +
  '</select>' +
  '</fieldset>' +
  '<button type="submit">Guardar</button>' +
  '</form>' +
  '<script>' +
  "var qs=new URLSearchParams(location.search);" +
  "var rt=qs.get('return_to')||'pebble://event/webviewclosed';" +
  "document.querySelector('form').addEventListener('submit',function(ev){ev.preventDefault();var d=new FormData(ev.target);var s={zona:parseInt(d.get('zona'),10),unidad:parseInt(d.get('unidad'),10)};location.href=rt+encodeURIComponent(JSON.stringify(s));});" +
  '</script></body></html>';

function pad2(n) {
  return (n < 10 ? '0' : '') + n;
}

function ahoraMadrid(canarias) {
  var now = new Date();
  var utcMs = now.getTime();
  var y = now.getUTCFullYear();
  var domMar = new Date(Date.UTC(y, 2, 31));
  var domOct = new Date(Date.UTC(y, 9, 31));
  var inicioCest = Date.UTC(y, 2, 31 - domMar.getUTCDay(), 1, 0, 0);
  var finCest = Date.UTC(y, 9, 31 - domOct.getUTCDay(), 1, 0, 0);
  var offset = ((utcMs >= inicioCest && utcMs < finCest) ? 2 : 1) - (canarias ? 1 : 0);
  return new Date(utcMs + offset * 3600000);
}

function leerAjustes() {
  try {
    var raw = localStorage.getItem(CLAVE_AJUSTES);
    if (raw) {
      var a = JSON.parse(raw);
      return { zona: a.zona | 0, unidad: a.unidad | 0 };
    }
  } catch (e) {
    console.log('PVPC: ajustes locales ilegibles ' + e);
  }
  return { zona: 0, unidad: 0 };
}

function guardarAjustes(ajustes) {
  try {
    localStorage.setItem(CLAVE_AJUSTES, JSON.stringify(ajustes));
  } catch (e) {
    console.log('PVPC: no se pudieron guardar los ajustes ' + e);
  }
}

Pebble.addEventListener('ready', function () {
  console.log('PVPC: PebbleKit JS listo');
  var msg = {};
  msg[KEY.REQ] = JS_READY;
  Pebble.sendAppMessage(msg, function () {
    console.log('PVPC: señal de listo entregada al reloj');
  }, function (e) {
    console.log('PVPC: fallo al enviar señal de listo ' + e.error);
  });
});

Pebble.addEventListener('showConfiguration', function () {
  var a = leerAjustes();
  var html = PAGINA_CONFIG;
  for (var i = 0; i < NOMBRES_ZONA.length; i++) {
    html = html.replace('__Z' + i + '__', a.zona === i ? 'selected' : '');
  }
  html = html.replace('__U0__', a.unidad === 0 ? 'selected' : '');
  html = html.replace('__U1__', a.unidad === 1 ? 'selected' : '');
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response || e.response === 'CANCELLED') {
    return;
  }
  try {
    var ajustes = JSON.parse(decodeURIComponent(e.response));
    ajustes.zona = ajustes.zona | 0;
    ajustes.unidad = ajustes.unidad | 0;
    if (ajustes.zona < 0 || ajustes.zona >= ZONAS.length) {
      ajustes.zona = 0;
    }
    if (ajustes.unidad !== 0 && ajustes.unidad !== 1) {
      ajustes.unidad = 0;
    }
    guardarAjustes(ajustes);
    console.log('PVPC: ajustes guardados ' + JSON.stringify(ajustes));
    var msg = {};
    msg[KEY.GEO] = ajustes.zona;
    msg[KEY.UNIDAD] = ajustes.unidad;
    Pebble.sendAppMessage(msg, function () {
      console.log('PVPC: ajustes enviados al reloj');
    }, function (err) {
      console.log('PVPC: fallo al enviar ajustes ' + err.error);
    });
  } catch (ex) {
    console.log('PVPC: respuesta de configuración no válida');
  }
});

Pebble.addEventListener('appmessage', function (e) {
  var payload = e.payload || {};
  console.log('PVPC: appmessage recibido ' + JSON.stringify(payload));

  var t = payload[KEY.ERR];
  if (t !== undefined) {
    enviarError(String(t));
    return;
  }

  if (payload[KEY.REQ] !== undefined) {
    var geoIdx = payload[KEY.GEO];
    var geo = 'peninsular';
    if (typeof geoIdx === 'number' && geoIdx >= 0 && geoIdx < ZONAS.length) {
      geo = ZONAS[geoIdx];
    }
    pedirPrecios(geo);
    return;
  }

  if (payload[KEY.PRICES] !== undefined) {
    var horaActual = 0;
    if (typeof payload[KEY.HORA] === 'number') {
      horaActual = payload[KEY.HORA];
    }
    procesarDatos(payload, horaActual);
    return;
  }

  var geoSync = payload[KEY.GEO];
  var unidad = payload[KEY.UNIDAD];
  if (geoSync !== undefined || unidad !== undefined) {
    var ajustes = leerAjustes();
    if (typeof geoSync === 'number' && geoSync >= 0 && geoSync < ZONAS.length) {
      ajustes.zona = geoSync;
    }
    if (unidad === 0 || unidad === 1) {
      ajustes.unidad = unidad;
    }
    guardarAjustes(ajustes);
    console.log('PVPC: ajustes sincronizados ' + JSON.stringify(ajustes));
  }
});

function peticion(url, cb) {
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.onload = function () {
    var json = null;
    try {
      json = JSON.parse(req.responseText);
    } catch (err) {
      json = null;
    }
    if (req.status === 200 && json) {
      cb(null, json);
    } else if (json && json.errors && json.errors[0] && json.errors[0].detail) {
      cb(json.errors[0].detail);
    } else {
      cb('Error del servidor (' + req.status + ')');
    }
  };
  req.onerror = function () {
    cb('Error de red: no se pudo contactar con Red Eléctrica');
  };
  req.send(null);
}

function fechaISO(d) {
  return d.getUTCFullYear() + '-' + pad2(d.getUTCMonth() + 1) + '-' + pad2(d.getUTCDate());
}

function pedirPrecios(geo) {
  var m = ahoraMadrid(geo === 'canarias');
  var fechaHoy = fechaISO(m);
  var man = new Date(Date.UTC(m.getUTCFullYear(), m.getUTCMonth(), m.getUTCDate() + 1));
  var fechaManana = fechaISO(man);
  var hora = m.getUTCHours();
  var urlHoy = API_URL + '?start_date=' + fechaHoy + 'T00:00&end_date=' + fechaHoy +
               'T23:59&time_trunc=hour&geo_limit=' + geo;
  var urlManana = API_URL + '?start_date=' + fechaManana + 'T00:00&end_date=' + fechaManana +
                  'T23:59&time_trunc=hour&geo_limit=' + geo;
  console.log('PVPC: hoy ' + urlHoy);
  console.log('PVPC: mañana ' + urlManana);

  var datosHoy = null;
  var errorHoy = null;
  var datosManana = null;
  var pendientes = 2;

  function terminar() {
    if (datosHoy === null) {
      enviarError(errorHoy || 'Sin datos para hoy');
      return;
    }
    if (datosManana === null) {
      console.log('PVPC: sin datos de mañana (' + 'aún no publicados' + ')');
    }
    try {
      procesarRespuesta(datosHoy, datosManana, hora, fechaHoy, fechaManana, geo === 'canarias');
    } catch (ex) {
      console.log('PVPC: excepción al procesar ' + ex);
      enviarError('Error al procesar los datos');
    }
  }

  peticion(urlHoy, function (err, json) {
    if (err) {
      errorHoy = err;
      console.log('PVPC: error hoy ' + err);
    } else {
      datosHoy = json;
    }
    pendientes--;
    if (pendientes === 0) {
      terminar();
    }
  });
  peticion(urlManana, function (err, json) {
    if (err) {
      console.log('PVPC: error mañana ' + err);
    } else {
      datosManana = json;
    }
    pendientes--;
    if (pendientes === 0) {
      terminar();
    }
  });
}

function extraerValores(json) {
  if (!json || !json.included || !json.included.length) {
    return null;
  }
  for (var i = 0; i < json.included.length; i++) {
    var inc = json.included[i];
    if (inc && inc.attributes && inc.attributes.title === 'PVPC' &&
        inc.attributes.values && inc.attributes.values.length) {
      return inc.attributes.values;
    }
  }
  for (i = 0; i < json.included.length; i++) {
    var inc2 = json.included[i];
    if (inc2 && inc2.attributes && inc2.attributes.values && inc2.attributes.values.length) {
      return inc2.attributes.values;
    }
  }
  return null;
}

function procesarRespuesta(jsonHoy, jsonManana, horaActual, fechaHoy, fechaManana, canarias) {
  var valores = [];
  for (var i = 0; i < 48; i++) {
    valores.push(SIN_DATO);
  }
  procesarDia(jsonHoy, fechaHoy, valores, 0);
  procesarDia(jsonManana, fechaManana, valores, 24);

  if (canarias) {
    // La hora canaria h corresponde a la hora peninsular h+1 (misma hora física),
    // así que la serie se desplaza: la fila h muestra el precio peninsular de h+1.
    for (i = 0; i < 47; i++) {
      valores[i] = valores[i + 1];
    }
    valores[47] = SIN_DATO;
  }

  var horaMin = -1;
  var horaMax = -1;
  var valMin = null;
  var valMax = null;
  for (i = 0; i < 24; i++) {
    if (valores[i] === SIN_DATO) {
      continue;
    }
    if (valMin === null || valores[i] < valMin) {
      valMin = valores[i];
      horaMin = i;
    }
    if (valMax === null || valores[i] > valMax) {
      valMax = valores[i];
      horaMax = i;
    }
  }

  var hora = horaActual;
  if (hora < 0 || hora > 23 || valores[hora] === SIN_DATO) {
    var ultima = -1;
    for (i = 0; i < 24; i++) {
      if (valores[i] !== SIN_DATO) {
        ultima = i;
      }
    }
    hora = ultima;
  }
  if (hora < 0 || horaMin < 0) {
    enviarError('Sin datos para hoy');
    return;
  }
  var bytes = [];
  for (i = 0; i < 48; i++) {
    var x = valores[i];
    bytes.push(x & 255, (x >>> 8) & 255, (x >>> 16) & 255, (x >>> 24) & 255);
  }
  var msg = {};
  msg[KEY.PRICES] = bytes;
  msg[KEY.HORA] = hora;
  msg[KEY.MINH] = horaMin;
  msg[KEY.MAXH] = horaMax;
  Pebble.sendAppMessage(msg, function () {
    console.log('PVPC: datos enviados al reloj');
  }, function (e) {
    console.log('PVPC: fallo al enviar datos ' + e.error);
  });
}

function procesarDia(json, fecha, valores, base) {
  var serie = extraerValores(json);
  if (!serie) {
    return;
  }
  for (var i = 0; i < serie.length; i++) {
    var v = serie[i];
    if (!v || v.value === null || v.value === undefined || !v.datetime) {
      continue;
    }
    if (v.datetime.substr(0, 10) !== fecha) {
      continue;
    }
    var h = parseInt(v.datetime.substr(11, 2), 10);
    if (isNaN(h) || h < 0 || h > 23) {
      continue;
    }
    valores[base + h] = Math.round(v.value * 100);
  }
}

function procesarDatos(payload, horaActual) {
  var bytes = payload[KEY.PRICES];
  var precios = [];
  for (var h = 0; h < 24; h++) {
    precios.push(bytes[h * 4] | (bytes[h * 4 + 1] << 8) | (bytes[h * 4 + 2] << 16) | (bytes[h * 4 + 3] << 24));
  }
  console.log('PVPC: precios recibidos, hora actual ' + horaActual +
              ', mín ' + payload[KEY.MINH] + ', máx ' + payload[KEY.MAXH]);
}

function enviarError(texto) {
  console.log('PVPC: ' + texto);
  var msg = {};
  msg[KEY.ERR] = texto;
  Pebble.sendAppMessage(msg, function () {}, function (e) {
    console.log('PVPC: fallo al enviar error ' + e.error);
  });
}
