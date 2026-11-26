# Parte 1 - Investigação de desempenho

Medimos FPS, CPU e latência nas pipelines locais e em transmissões UDP entre dois computadores. Localmente, o FPS foi mantido sem descartes. Na rede, observamos demora para iniciar o vídeo, atraso do codificador, FPS menor no receptor e perda de pacotes, com pixelização e *ghosting*.

## Ambiente e método

Os testes locais foram feitos no computador abaixo, que também foi o transmissor na segunda bateria:

```
Item       |Valor
           |
CPU        |Intel Core i5-13450HX (10 núcleos / 16 threads)
Sistema    |Linux Mint 22.3, sessão X11
GStreamer  |1.24.2 (sink real: xvimagesink, via autovideosink)
VLC        |3.0.20
```

Cada teste durou 10 s. A base foi 640x360, 25 fps, 500 kbps e `speed-preset=ultrafast`; alteramos uma variável por vez.

O `fpsdisplaysink` informou FPS e quadros renderizados ou descartados. Usamos `text-overlay=false` para não incluir o custo do texto, medido à parte.

O `/usr/bin/time` mediu a CPU média do `gst-launch`; 100% equivale a um núcleo. Também informamos o percentual sobre as 16 threads da máquina.

A latência veio do tracer `latency` do GStreamer, da fonte ao sink.

Os scripts e logs estão em `experimentos` no repositório.

## 1.1 Reprodução local e monitoramento de FPS

Executamos a pipeline abaixo alterando a resolução e a taxa de quadros:

```
/usr/bin/time -f "CPU=%P" gst-launch-1.0 -e -v videotestsrc is-live=true pattern=ball ! video/x-raw,width=640,height=360,framerate=25/1 ! videoconvert ! fpsdisplaysink text-overlay=false
```

```
Configuração            |Rend./Desc.  |FPS    |CPU (1 núcleo)  |CPU (total)
                        |             |       |                |
320x240 @ 25            |248 / 0      |25,06  |2%              |0,1%
640x360 @ 25            |248 / 0      |25,05  |6%              |0,4%
1280x720 @ 25           |247 / 0      |25,03  |20%             |1,3%
640x360 @ 15            |145 / 0      |15,07  |4%              |0,3%
640x360 @ 30            |294 / 0      |30,04  |8%              |0,5%
640x360 @ 25 + overlay  |248 / 0      |25,05  |8%              |0,5%
```

Todas as configurações mantiveram o FPS e não descartaram quadros. A CPU cresceu com pixels por segundo: de 320x240 para 1280x720, a imagem tem 12 vezes mais pixels e o consumo subiu cerca de 10 vezes. O overlay elevou a CPU de 6% para 8%.

## 1.2 Impacto da codificação e decodificação

Em seguida adicionamos a codificação e a decodificação H.264 na mesma pipeline:

```
/usr/bin/time -f "CPU=%P" gst-launch-1.0 -e -v videotestsrc is-live=true pattern=ball ! video/x-raw,width=640,height=360,framerate=25/1 ! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=500 ! h264parse ! avdec_h264 ! videoconvert ! fpsdisplaysink text-overlay=false
```

Testamos cada variável isoladamente e um cenário de estresse: 1280x720, 30 fps, 1500 kbps e preset `medium`.

```
Variável      |Valor                               |Rend./Desc.  |FPS    |CPU (1 núcleo)  |CPU (total)
              |                                    |             |       |                |
base          |640x360, 25fps, 500kbps, ultrafast  |248 / 0      |25,09  |30%             |1,9%
bitrate       |200 kbps                            |248 / 0      |25,05  |30%             |1,9%
bitrate       |1500 kbps                           |247 / 0      |25,05  |28%             |1,8%
speed-preset  |fast                                |248 / 0      |25,08  |42%             |2,6%
speed-preset  |medium                              |248 / 0      |25,05  |46%             |2,9%
framerate     |15/1                                |145 / 0      |15,09  |17%             |1,1%
framerate     |30/1                                |296 / 0      |30,06  |32%             |2,0%
resolução     |320x240                             |248 / 0      |25,06  |12%             |0,8%
resolução     |1280x720                            |248 / 0      |25,10  |73%             |4,6%
estresse      |1280x720, 30fps, 1500kbps, medium   |293 / 0      |29,89  |143%            |8,9%
```

O FPS foi mantido sem descartes. Na base, H.264 elevou a CPU de 6% para 30%. Bitrate quase não alterou o consumo; resolução, FPS e preset tiveram impacto maior. O `medium` consumiu cerca de 1,5× o `ultrafast`, e 1280x720 chegou a 73%. O teste de estresse usou 143% (mais de um núcleo), mas manteve 30 fps.

Também medimos a latência com e sem a opção tune=zerolatency:

```
x264enc           |Latência média
                  |
tune=zerolatency  |2,5 ms
sem tune          |440,6 ms
```

Sem `zerolatency`, o x264 usa *lookahead* e B-frames, acumulando cerca de 440 ms de atraso. O FPS permanece igual, mas o vídeo fica defasado da fonte.

## 1.3 Investigação da transmissão pela rede

Usamos a pipeline MPEG-TS/UDP da atividade anterior, com transmissor e receptor no mesmo computador (127.0.0.1). Depois repetimos os testes com as propriedades sugeridas na atividade.

Transmissor original:

```
/usr/bin/time -f "CPU=%P" gst-launch-1.0 -e videotestsrc is-live=true pattern=ball ! video/x-raw,width=640,height=360,framerate=25/1 ! videoconvert ! x264enc speed-preset=ultrafast bitrate=500 ! h264parse ! mpegtsmux ! udpsink host=127.0.0.1 port=5000
```

Transmissor modificado:

```
/usr/bin/time -f "CPU=%P" gst-launch-1.0 -e videotestsrc is-live=true pattern=ball ! video/x-raw,width=640,height=360,framerate=25/1 ! videoconvert ! x264enc speed-preset=ultrafast bitrate=500 tune=zerolatency key-int-max=25 ! h264parse config-interval=-1 ! mpegtsmux alignment=7 ! udpsink host=127.0.0.1 port=5000 sync=false
```

Receptor GStreamer:

```
/usr/bin/time -f "CPU=%P" gst-launch-1.0 -e -v udpsrc port=5000 caps="video/mpegts,systemstream=(boolean)true,packetsize=(int)188" ! tsdemux ! h264parse ! avdec_h264 ! videoconvert ! queue ! fpsdisplaysink text-overlay=false
```

### Propriedades testadas

| Elemento | Propriedade | Para que serve | O que observamos |
| --- | --- | --- | --- |
| x264enc | key-int-max=25 | Define a distância máxima entre quadros-chave (IDR). Com 25 fps, fica um por segundo. Sem ela, o x264 usa 250 quadros, ou seja, 10 s. | Quem entra no meio da transmissão só começa a decodificar no próximo quadro-chave. |
| x264enc | tune=zerolatency | Desliga os B-frames e o lookahead, então cada quadro sai assim que é codificado. | A latência do codificador caiu de 440,6 ms para 2,5 ms. |
| h264parse | config-interval=-1 | Repete o SPS/PPS antes de cada quadro-chave. Sem esses dados o decodificador não consegue começar. | O receptor recebe o SPS/PPS a cada segundo, e não só no começo do fluxo. |
| mpegtsmux | alignment=7 | Junta 7 pacotes TS de 188 bytes em um datagrama de 1316 bytes, que cabe na MTU de 1500 bytes. | Em 4 s foram 642 datagramas de 188 bytes sem a opção e 92 de 1316 bytes com ela. |
| udpsink | sync=false | Envia os dados assim que ficam prontos, sem esperar o relógio da pipeline. | Não notamos diferença no FPS em localhost. |

### Resultados com o receptor GStreamer

Testamos dois casos para cada pipeline: transmissor e receptor iniciados juntos, e o receptor iniciado 3 segundos depois do transmissor. Cada receptor rodou por 10 s.

| Teste | Renderizados / descartados | FPS | CPU RX | CPU TX | Observação |
| --- | --- | --- | --- | --- | --- |
| Original, iniciados juntos | 198 / 0 | 23,3 | 6% | 22% | Reprodução contínua |
| Original, receptor 3 s depois | 41 / 0 | 25,0 | 2% | 21% | Cerca de 8 s sem vídeo, esperando o próximo quadro-chave |
| Modificada, iniciados juntos | 186 / 0 | 23,6 | 6% | 21% | Reprodução contínua |
| Modificada, receptor 3 s depois | 186 / 0 | 23,6 | 7% | 23% | Mesmo resultado do início simultâneo |

Na pipeline original, iniciar o receptor depois resultou em apenas 41 quadros (1,6 s de vídeo) e cerca de 8 s sem imagem. A modificada teve resultado semelhante ao início simultâneo.

### Resultados com o VLC

Abrimos o VLC (udp://@:5000) 3 segundos depois do transmissor e medimos quanto tempo ele levou para mostrar o vídeo.

```
Transmissor  |SPS/PPS encontrado  |Saída de vídeo aberta
             |                    |
Original     |7,53 s              |7,63 s
Modificada   |1,08 s              |1,13 s
```

Sem SPS/PPS, o VLC registra `waiting for SPS/PPS` e não exibe vídeo. A espera caiu de 7,5 s para pouco mais de 1 s com a pipeline modificada.

### FPS abaixo do esperado no receptor

Nos testes por UDP o receptor ficou em torno de 23,5 fps, mesmo com o transmissor enviando 25. Junto com isso, o GStreamer mostrava o aviso "Pipeline construction is invalid, please add queues". Para investigar, mantivemos o transmissor modificado e mudamos só o receptor:

```
Receptor                         |Rend./Desc.  |FPS   |CPU RX
                                 |             |      |
Sem queue                        |186 / 0      |23,4  |6%
Sem queue, avdec_h264 qos=false  |186 / 0      |23,5  |7%
Com queue antes do sink          |223 / 0      |25,0  |8%
Com queue, sink sync=false       |212 / 0      |24,8  |8%
Com queue, tsdemux latency=0     |220 / 0      |25,0  |7%
```

Com `queue` antes do sink, o receptor voltou a 25 fps. Sem ela, o sink bloqueia a thread durante a sincronização. A origem exata da perda não foi confirmada: desativar QoS não mudou o resultado e `UdpRcvbufErrors` não aumentou.

### Transmissão entre dispositivos

Na segunda bateria de testes repetimos a transmissão UDP entre dois computadores na mesma rede local. O computador mais novo foi o transmissor e um notebook mais antigo foi o receptor.

Máquina transmissora:

```
CPU        |Intel Core i5-13450HX
Sistema    |Linux Mint 22.3
RAM        |16 GB
GStreamer  |1.24.2
VLC        |3.0.20
```

Máquina receptora:

```
CPU        |Intel Core i3-2370M
Sistema    |Linux Mint 22.3
RAM        |4 GB
GStreamer  |1.24.2
VLC        |3.0.20
```

Usamos o mesmo receptor GStreamer da seção anterior (com queue) e os transmissores original e modificado, trocando o host do udpsink pelo IP do receptor. Testamos 15 e 60 fps, sempre em 640x360 e 500 kbps. Nos testes 5 e 6 o receptor foi iniciado alguns segundos depois do transmissor, e no teste 7 usamos o VLC como receptor.

| Teste | FPS médio (RX) | Renderizados / descartados | CPU TX | CPU RX | Observação |
| --- | --- | --- | --- | --- | --- |
| 1 Original 15 fps | 13,65 | 840 / 0 | 14% | 4% | Imagem com blocos e artefatos; aviso de perda de pacotes no tsdemux |
| 2 Original 60 fps | 46,05 | 1640 / 0 | 57% | 17% | FPS oscilando entre 31 e 60, imagem borrada |
| 3 Modificada 15 fps | 13,95 | 711 / 0 | 14% | 11% | Visualmente lento, mas não chegou a pixelar |
| 4 Modificada 60 fps | 55,72 | 2757 / 0 | 54% | 18% | Mais fluido, com ghosting; pixelou poucas vezes |
| 5 Original 60 fps, receptor depois | 46,12 | 1752 / 0 | 52% | 20% | Vídeo apareceu em cerca de 10 s; bem pixelado e travando |
| 6 Modificada 15 fps, receptor depois | 14,69 | 1267 / 0 | 14% | 12% | Vídeo apareceu em cerca de 2 s; mais fluido, com ghosting |
| 7 VLC (modificada 15 fps) | não medido | não medido | 14% | não medido | Leves travamentos e ghosting |

Não houve descartes nem saturação de CPU, mas houve blocos, pixelização e *ghosting*. O aviso `CONTINUITY: Mismatch packet` nos testes 1 e 4 confirma perda de pacotes MPEG-TS na rede.

A 60 fps, a modificada foi mais estável: 55,72 fps (57–59 nos últimos segundos), contra 46,05 fps e oscilação de 31–60 na original. Como a modificada produz quadro-chave a cada segundo, recuperou-se mais rápido das perdas; na original, isso podia levar 4 s a 60 fps ou 17 s a 15 fps. Com o receptor iniciado depois, o vídeo apareceu em cerca de 2 s, contra 10 s na original.

## 1.4 Registro e análise dos resultados

| Experimento | FPS | CPU | Fluidez / observações |
| --- | --- | --- | --- |
| Local sem compressão | 25 | 6% (0,4% do total) | Manteve 25 FPS constantes. 248 quadros renderizados e nenhum descartado (640x360). Em 1280x720 também manteve 25 FPS, com 20% de CPU. |
| Local com H.264 | 25 | 30% (1,9% do total) | Manteve 25 FPS constantes. 248 quadros renderizados e nenhum descartado. Em 1280x720 a CPU subiu para 73%, e em 720p/30 fps/medium chegou a 143%, ainda sem descartes. |
| UDP via localhost | 23,3 | RX 6% + TX 22% (1,8% do total) | Reprodução contínua, mas abaixo de 25 FPS (198 renderizados, nenhum descartado). Com o receptor entrando 3 s depois, ficou cerca de 8 s sem vídeo (no VLC, 7,5 s). |
| UDP entre dispositivos | 46,05 (60 fps) e 13,65 (15 fps) | TX 57% + RX 17% (60 fps) | Pipeline original. Nenhum quadro descartado, mas a imagem ficou com blocos e pixelada por perda de pacotes na rede. A 60 fps o FPS oscilou entre 31 e 60. Com o receptor entrando depois, o vídeo levou cerca de 10 s para aparecer. |
| Pipeline modificada | Localhost: 23,6 (25 com queue). Entre dispositivos: 55,72 (60 fps) e 13,95 (15 fps) | Entre dispositivos: TX 54% + RX 18% (60 fps) | Mais fluida que a original a 60 fps, com o FPS entre 57 e 59. Ainda houve ghosting por perda de pacotes, mas a imagem pixelou poucas vezes e se recuperou rápido. Com o receptor entrando depois, o vídeo apareceu em cerca de 1 s no localhost e 2 s entre dispositivos. |

\* Realizar somente quando a infraestrutura permitir.

### Primeira bateria de testes

Antes dos testes descritos acima, fizemos uma primeira bateria em outro computador, um notebook Vivobook cujas especificações não anotamos. Todos os experimentos foram feitos a 60 fps, usando a porta 5001. Os resultados foram:

| Experimento | FPS | CPU | Fluidez / observações |
| --- | --- | --- | --- |
| Local sem compressão | 60 | 2-3% | Manteve constante 60 FPS. 4000 renderizados, 2 dropados |
| Local com H.264 | 60 | 3-5% | Manteve constante 60 FPS. 6000 renderizados, 1 dropado |
| UDP via localhost | 60 | Indisponível | Manteve conexão e reprodução constante com 60 FPS |
| UDP entre dispositivos\* | 60 | Indisponível | Por conta de problemas na rede, a conexão e a reprodução não eram constantes. Quando conectava, funcionava com 15 FPS, mas com 60 FPS travava |
| Pipeline modificada | 60 | Indisponível | Por conta de problemas na rede, a conexão e a reprodução não eram constantes |

Nessa bateria, a rede impediu uma comparação confiável. Por isso repetimos os testes na seção 1.3, incluindo CPU.

### Análise

Sem rede, todas as pipelines mantiveram o FPS configurado, sem descartes. Em UDP, identificamos quatro causas de degradação:

- Inicialização tardia: na pipeline original, o receptor espera um quadro-chave e SPS/PPS; com `key-int-max` igual ao FPS e `config-interval=-1`, o vídeo passou a iniciar em 1–2 s.
- Latência do encoder: `tune=zerolatency` reduziu a média de 440,6 ms para 2,5 ms.
- Receptor local: uma `queue` antes do sink elevou o FPS de cerca de 23,5 para 25.
- Rede: perdas de pacotes causaram pixelização e *ghosting* entre máquinas. A pipeline modificada não evita perdas, mas limita a recuperação a cerca de 1 s e foi mais estável a 60 fps (55,72 contra 46,05).

| Problema | Tipo | Etapa | Situação |
| --- | --- | --- | --- |
| Vídeo demora vários segundos para aparecer | Ausência de vídeo | Decodificação no receptor | Confirmado e corrigido com key-int-max e config-interval |
| Vídeo atrasado em relação à fonte | Atraso | Codificação | Confirmado e corrigido com tune=zerolatency |
| Receptor local em 23,5 fps | Baixa fluidez | Receptor | Corrigido com queue, mas a causa exata não foi confirmada |
| Imagem pixelada e com ghosting entre dispositivos | Baixa fluidez e artefatos | Rede | Confirmado (perda de pacotes MPEG-TS); a pipeline modificada reduziu, mas não eliminou |
| Queda de FPS por falta de CPU | Baixa fluidez | Codificação e decodificação | Não aconteceu; o notebook receptor usou no máximo 20% de CPU |

Algumas hipóteses ficaram sem confirmação. Não sabemos se uma máquina mais fraca perderia quadros na codificação, já que o transmissor usado nos testes é rápido e o notebook mais antigo só foi usado como receptor. Também não separamos o efeito de cada propriedade na rede real, porque a pipeline modificada foi testada com todas as propriedades juntas.
