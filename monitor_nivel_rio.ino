
/*
  ============================================================
  MONITOR DE NÍVEL DE RIO - ESP32 + HC-SR04 (ou JSN-SR04T)
  ============================================================
  O ESP32 conecta na sua rede Wi-Fi e hospeda a própria página
  web. Qualquer aparelho na mesma rede acessa o IP do ESP32
  (mostrado no Serial Monitor ao ligar) e vê o nível da água
  em tempo real, com gráfico do histórico recente.

  IMPORTANTE - HARDWARE PARA USO EM RIO:
  O sensor HC-SR04 comum NÃO é à prova d'água e tem alcance
  curto (~4m no melhor caso, geralmente menos). Para instalar
  sobre um rio de verdade, o recomendado é o JSN-SR04T
  (versão waterproof, alcance até ~4-6m), que usa o MESMO
  protocolo de trigPin/echoPin — este código funciona para
  os dois sem alterações de lógica.

  MODO WI-FI: ACCESS POINT (AP)
  O ESP32 CRIA a própria rede Wi-Fi (não precisa de roteador
  nem internet no local). Depois de ligar:
    1) No celular/PC, procure a rede definida em apSSID abaixo
    2) Conecte usando a senha em apPassword
    3) Abra o navegador em http://192.168.4.1

  Ajuste antes de gravar:
    1) apSSID / apPassword -> nome e senha da rede que o
       ESP32 vai criar
    2) alturaSensorCm  -> distância do sensor até o nível
       "zero" (leito do rio ou marca de referência que você
       escolher)
    3) nivelAtencaoCm / nivelAlertaCm -> limites de aviso
  ============================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// ---------------- CONFIGURAÇÃO WI-FI (Access Point) ----------------
const char* apSSID     = "MonitorRio";      // nome que vai aparecer na lista de redes
const char* apPassword = "monitor123";      // minimo 8 caracteres, ou "" para rede aberta

// ---------------- PINOS DO SENSOR ----------------
const int trigPin = 5;
const int echoPin = 18;

// ---------------- CALIBRAÇÃO DO NÍVEL ----------------
// Distância (cm) do sensor até o ponto que você considera "nível 0".
// Meça na instalação: fita métrica do sensor até a água (ou leito) em dia normal.
float alturaSensorCm   = 300.0;  // EXEMPLO: sensor 3m acima do nível zero
float nivelAtencaoCm   = 180.0;  // a partir daqui mostra "Atenção"
float nivelAlertaCm    = 240.0;  // a partir daqui mostra "Alerta - risco de cheia"

// ---------------- HISTÓRICO EM MEMÓRIA ----------------
#define HIST_TAM 40
float historico[HIST_TAM];
int histIndex = 0;
bool histCheio = false;

const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

// Leitura filtrada (mediana de 5 amostras, ignora falhas)
long lerDistanciaBruta() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  return duration;
}

float medirDistanciaCm() {
  const int N = 5;
  long amostras[N];
  int validas = 0;

  for (int i = 0; i < N; i++) {
    long d = lerDistanciaBruta();
    if (d > 0) {
      amostras[validas++] = d;
    }
    delay(30);
  }

  if (validas == 0) return -1.0; // sem leitura válida

  // ordena (bubble sort simples, array pequeno)
  for (int i = 0; i < validas - 1; i++) {
    for (int j = 0; j < validas - i - 1; j++) {
      if (amostras[j] > amostras[j + 1]) {
        long tmp = amostras[j];
        amostras[j] = amostras[j + 1];
        amostras[j + 1] = tmp;
      }
    }
  }

  long mediana = amostras[validas / 2];
  return mediana * 0.0343 / 2.0; // cm
}

String statusNivel(float nivelCm) {
  if (nivelCm >= nivelAlertaCm) return "ALERTA";
  if (nivelCm >= nivelAtencaoCm) return "ATENCAO";
  return "NORMAL";
}

void salvarHistorico(float nivelCm) {
  historico[histIndex] = nivelCm;
  histIndex = (histIndex + 1) % HIST_TAM;
  if (histIndex == 0) histCheio = true;
}

// ---------------- ROTA: /data (JSON) ----------------
void handleData() {
  float distancia = medirDistanciaCm();
  String json = "{";

  if (distancia < 0) {
    json += "\"ok\":false";
  } else {
    float nivel = alturaSensorCm - distancia;
    if (nivel < 0) nivel = 0;
    float percentual = (nivel / nivelAlertaCm) * 100.0;
    if (percentual > 100) percentual = 100;
    if (percentual < 0) percentual = 0;

    salvarHistorico(nivel);

    json += "\"ok\":true,";
    json += "\"distancia\":" + String(distancia, 1) + ",";
    json += "\"nivel\":" + String(nivel, 1) + ",";
    json += "\"percentual\":" + String(percentual, 1) + ",";
    json += "\"status\":\"" + statusNivel(nivel) + "\",";

    // histórico
    json += "\"historico\":[";
    int total = histCheio ? HIST_TAM : histIndex;
    int inicio = histCheio ? histIndex : 0;
    for (int i = 0; i < total; i++) {
      int idx = (inicio + i) % HIST_TAM;
      json += String(historico[idx], 1);
      if (i < total - 1) json += ",";
    }
    json += "]";
  }

  json += "}";
  server.send(200, "application/json", json);
}

// ---------------- ROTA: / (página HTML) ----------------
void handleRoot() {
  String html = R"HTML(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Nivel do Rio</title>
<style>
  :root { --normal:#2e9e5b; --atencao:#e0a419; --alerta:#d9432e; --bg:#0f1720; --card:#182634; --txt:#e8eef4; }
  * { box-sizing: border-box; }
  body { margin:0; font-family: -apple-system, Segoe UI, Roboto, sans-serif; background:var(--bg); color:var(--txt); padding:20px; }
  h1 { font-size: 1.3em; text-align:center; margin-bottom: 4px; }
  .sub { text-align:center; color:#8fa3b3; font-size:0.85em; margin-bottom:20px; }
  .card { background:var(--card); border-radius:16px; padding:20px; margin-bottom:16px; box-shadow:0 4px 12px rgba(0,0,0,0.3); }
  .status { display:inline-block; padding:6px 16px; border-radius:20px; font-weight:600; font-size:0.95em; }
  .status.NORMAL { background:var(--normal); }
  .status.ATENCAO { background:var(--atencao); color:#1a1a1a; }
  .status.ALERTA { background:var(--alerta); }
  .row { display:flex; justify-content:space-between; align-items:center; margin-bottom:14px; }
  .metric { text-align:center; flex:1; }
  .metric .val { font-size:2em; font-weight:700; }
  .metric .lbl { color:#8fa3b3; font-size:0.8em; text-transform:uppercase; letter-spacing:0.5px; }
  .barra-container { background:#0f1720; border-radius:10px; height:24px; overflow:hidden; margin-top:6px; }
  .barra { height:100%; transition: width 0.5s ease, background 0.5s ease; }
  canvas { width:100%; height:140px; display:block; }
  .updated { text-align:center; color:#5c7180; font-size:0.75em; margin-top:8px; }
  .offline { text-align:center; color:var(--alerta); padding:20px; }
</style>
</head>
<body>
  <h1>Monitoramento de Nivel - Rio</h1>
  <div class="sub">ESP32 + Sensor Ultrassonico</div>

  <div class="card">
    <div class="row">
      <div class="metric">
        <div class="val" id="nivel">--</div>
        <div class="lbl">Nivel (cm)</div>
      </div>
      <div class="metric">
        <div id="status" class="status NORMAL">--</div>
      </div>
      <div class="metric">
        <div class="val" id="distancia">--</div>
        <div class="lbl">Distancia sensor-agua (cm)</div>
      </div>
    </div>
    <div class="barra-container">
      <div class="barra" id="barra" style="width:0%; background:var(--normal);"></div>
    </div>
    <div class="updated" id="updated">Aguardando leitura...</div>
  </div>

  <div class="card">
    <div class="lbl" style="margin-bottom:8px;">Historico recente</div>
    <canvas id="grafico"></canvas>
  </div>

<script>
const canvas = document.getElementById('grafico');
const ctx = canvas.getContext('2d');

function resizeCanvas() {
  canvas.width = canvas.clientWidth * devicePixelRatio;
  canvas.height = canvas.clientHeight * devicePixelRatio;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

function desenharGrafico(dados) {
  resizeCanvas();
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  if (dados.length < 2) return;

  const max = Math.max(...dados) * 1.1 || 1;
  const min = Math.min(0, Math.min(...dados));
  const passoX = w / (dados.length - 1);

  ctx.beginPath();
  ctx.strokeStyle = '#4da3ff';
  ctx.lineWidth = 2 * devicePixelRatio;
  dados.forEach((v, i) => {
    const x = i * passoX;
    const y = h - ((v - min) / (max - min || 1)) * h;
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();

  ctx.lineTo(w, h);
  ctx.lineTo(0, h);
  ctx.closePath();
  ctx.fillStyle = 'rgba(77,163,255,0.15)';
  ctx.fill();
}

function corStatus(status) {
  if (status === 'ALERTA') return getComputedStyle(document.documentElement).getPropertyValue('--alerta');
  if (status === 'ATENCAO') return getComputedStyle(document.documentElement).getPropertyValue('--atencao');
  return getComputedStyle(document.documentElement).getPropertyValue('--normal');
}

async function atualizar() {
  try {
    const res = await fetch('/data');
    const d = await res.json();
    if (!d.ok) {
      document.getElementById('updated').innerText = 'Sensor sem leitura (verifique fiacao)';
      return;
    }
    document.getElementById('nivel').innerText = d.nivel;
    document.getElementById('distancia').innerText = d.distancia;
    const statusEl = document.getElementById('status');
    statusEl.innerText = d.status;
    statusEl.className = 'status ' + d.status;

    const barra = document.getElementById('barra');
    barra.style.width = d.percentual + '%';
    barra.style.background = corStatus(d.status);

    document.getElementById('updated').innerText = 'Atualizado agora - ' + new Date().toLocaleTimeString();

    desenharGrafico(d.historico);
  } catch (e) {
    document.getElementById('updated').innerText = 'Erro ao conectar ao ESP32';
  }
}

atualizar();
setInterval(atualizar, 2000);
</script>
</body>
</html>
)HTML";

  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID);

  IPAddress ip = WiFi.softAPIP();

  Serial.println("================================");
  Serial.println("MONITOR DE NIVEL DE RIO");
  Serial.println("================================");
  Serial.print("WiFi: ");
  Serial.println(apSSID);
  Serial.print("IP: ");
  Serial.println(ip);

  // DNS captive portal
  dnsServer.start(DNS_PORT, "*", ip);

  // Servidor web
  server.on("/", handleRoot);
  server.on("/data", handleData);

  // Qualquer endereço acessado abre o monitor
  server.onNotFound([]() {
    handleRoot();
  });

  server.begin();

  Serial.println("Servidor iniciado!");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}
