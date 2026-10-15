/**
 * Recebe medições do ESP32 e as registra na aba "Medições do Rio".
 *
 * Instalação:
 * 1. Abra a planilha de destino e acesse Extensões > Apps Script.
 * 2. Cole este arquivo no editor e salve.
 * 3. Implante como Aplicativo da Web, executando como você e permitindo
 *    acesso a qualquer pessoa que possua o link.
 * 4. Copie a URL terminada em /exec para googleScriptURL no firmware.
 */

const NOME_ABA = "Medições do Rio";
const CABECALHOS = [
  "Data completa (dd/mm/aaaa)",
  "Hora (hh:mm:ss)",
  "Medição do nível (cm)",
  "Medição da distância (cm)",
  "Perigo",
  "Ultrassônico fazendo medição"
];

function doPost(e) {
  const lock = LockService.getScriptLock();

  try {
    if (!e || !e.postData || !e.postData.contents) {
      throw new Error("Requisição sem conteúdo JSON.");
    }

    const dados = JSON.parse(e.postData.contents);
    const planilha = SpreadsheetApp.getActiveSpreadsheet();
    if (!planilha) {
      throw new Error("Vincule este script à planilha de destino.");
    }

    lock.waitLock(5000);
    const aba = obterOuCriarAba_(planilha);
    const agora = new Date();
    const fuso = planilha.getSpreadsheetTimeZone();

    const nivel = numeroOuVazio_(dados.nivel);
    const distancia = numeroOuVazio_(dados.distancia);
    const perigo = String(dados.perigo || "SEM_LEITURA").toUpperCase();
    const ultrassonico =
      String(dados.ultrassonico || "").toLowerCase() === "ligado"
        ? "Ligado"
        : "Desligado";

    aba.appendRow([
      Utilities.formatDate(agora, fuso, "dd/MM/yyyy"),
      Utilities.formatDate(agora, fuso, "HH:mm:ss"),
      nivel,
      distancia,
      perigo,
      ultrassonico
    ]);

    const linha = aba.getLastRow();
    aba.getRange(linha, 1, 1, 2).setNumberFormat("@");
    aba.getRange(linha, 3, 1, 2).setNumberFormat("0.0");

    return respostaJson_({
      ok: true,
      mensagem: "Medição registrada.",
      linha: linha
    });
  } catch (erro) {
    return respostaJson_({
      ok: false,
      mensagem: String(erro && erro.message ? erro.message : erro)
    });
  } finally {
    if (lock.hasLock()) {
      lock.releaseLock();
    }
  }
}

function obterOuCriarAba_(planilha) {
  let aba = planilha.getSheetByName(NOME_ABA);
  if (!aba) {
    aba = planilha.insertSheet(NOME_ABA);
  }

  const primeiraLinha = aba.getRange(1, 1, 1, CABECALHOS.length);
  const existentes = primeiraLinha.getDisplayValues()[0];
  const linhaVazia = existentes.every(function (valor) {
    return valor === "";
  });

  if (linhaVazia) {
    primeiraLinha.setValues([CABECALHOS]);
    primeiraLinha
      .setFontWeight("bold")
      .setBackground("#17365D")
      .setFontColor("#FFFFFF")
      .setWrap(true);
    aba.setFrozenRows(1);
    aba.setColumnWidths(1, CABECALHOS.length, 190);
    aba.setColumnWidth(3, 170);
    aba.setColumnWidth(4, 190);
    aba.setColumnWidth(6, 250);
  } else if (
    CABECALHOS.some(function (titulo, indice) {
      return existentes[indice] !== titulo;
    })
  ) {
    throw new Error(
      'A primeira linha da aba "' +
        NOME_ABA +
        '" não corresponde aos cabeçalhos esperados.'
    );
  }

  return aba;
}

function numeroOuVazio_(valor) {
  if (valor === null || valor === undefined || valor === "") {
    return "";
  }

  const numero = Number(valor);
  return Number.isFinite(numero) ? numero : "";
}

function respostaJson_(objeto) {
  return ContentService.createTextOutput(JSON.stringify(objeto)).setMimeType(
    ContentService.MimeType.JSON
  );
}
