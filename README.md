# Mouse Sliding (OBS Plugin)

[![GitHub](https://img.shields.io/badge/GitHub-Discrutans%2Fmouse--sliding-blue?logo=github)](https://github.com/Discrutans/mouse-sliding)
[![Release](https://img.shields.io/github/v/release/Discrutans/mouse-sliding)](https://github.com/Discrutans/mouse-sliding/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

Нативный плагин OBS Studio — оверлей **коврика мыши** для FPS-стримов: aim-pad, шлейф, клики, комбо, колёсико, край холста и притягивание к центру.

**Репозиторий:** https://github.com/Discrutans/mouse-sliding  
**Лицензия:** [GPL-3.0](LICENSE)

## Download (без сборки)

Готовые сборки Windows x64: **[Releases](https://github.com/Discrutans/mouse-sliding/releases)**

1. Скачайте `mouse-sliding-windows-x64-v*-setup.exe`.
2. Запустите установщик и следуйте мастеру.
3. Перезапустите OBS → **Источники → + → Mouse Sliding**.

Установщик кладёт плагин в  
`C:\ProgramData\obs-studio\plugins\mouse-sliding\`.

## Возможности

- Pad-режим: aim следует за raw mouse (работает в fullscreen)
- Шлейф по скорости, клики ЛКМ/ПКМ, glow маркера
- Combo HIT / N HIT COMBO с шаблонами и шрифтами
- Колёсико — орбитальная дуга с инерцией
- Край холста: parallax + soft glow; притягивание к центру (idle / edge)
- Группы настроек, локали `en-US` / `ru-RU`
- Ссылки в свойствах: поддержка автора и GitHub

## Требования (использование)

- Windows 10/11 x64
- OBS Studio

## Сборка из исходников

Нужны Visual Studio 2022 (Desktop C++), CMake 3.28+, интернет при первой конфигурации.

```powershell
git clone https://github.com/Discrutans/mouse-sliding.git
cd mouse-sliding
.\build.ps1
```

DLL: `build_x64\rundir\RelWithDebInfo\mouse-sliding.dll`

Установщик Windows (Inno Setup 6):

```powershell
.\package-installer.ps1
```

Выход: `dist\mouse-sliding-windows-x64-v1.0.0-setup.exe`  
(после сборки нужна [Inno Setup 6](https://jrsoftware.org/isinfo.php); скрипт ставит её через winget при отсутствии).

## Структура

| Путь | Назначение |
|------|------------|
| `src/plugin-main.cpp` | Загрузка модуля |
| `src/mouse-sliding-source.cpp` | Физика + рендер |
| `data/` | locale + fonts |
| `installer/` | Inno Setup |
| `package-installer.ps1` | Сборка setup.exe |

Основано на [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).

## Поддержать автора

https://dalink.to/discrutans
