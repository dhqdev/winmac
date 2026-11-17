# Componentes de terceiros

O código do WinMac é MIT (veja `LICENSE`). Junto do executável vão estes arquivos de terceiros, cada um com a sua licença:

| Componente | Onde | Licença |
|---|---|---|
| **Inter** (fonte), por Rasmus Andersson e colaboradores — https://github.com/rsms/inter | `res/fonts/InterVariable.ttf` | SIL Open Font License 1.1 (`res/fonts/OFL.txt`) |
| **apple_cursor** (cursores estilo macOS), por Abdulkaiz Khatri — https://github.com/ful1e5/apple_cursor | `res/cursors/*.cur`, `*.ani` | GPL-3.0 (`res/cursors/LICENSE-GPL-3.0.txt`) |

Os cursores são arquivos de dados separados: o WinMac os copia para `%LOCALAPPDATA%\WinMac\cursors` quando você escolhe "Cursores estilo Mac". O código-fonte deles está no repositório original acima.

O mascote Pingo, os ícones e os papéis de parede são originais do WinMac e são desenhados por código (`src/art.cpp`).
