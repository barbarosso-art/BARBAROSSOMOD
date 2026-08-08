# BARBAROSSOMOD v0.1 — Quest

Quest-мод для Beat Saber `1.40.8_7379`. Мод читает `_customData._mapImage` из `Info.dat`
текущей пользовательской карты и загружает PNG/JPG/JPEG только из папки этой карты.
При переходе к другой песне объект предыдущего изображения уничтожается.

## Формат карты

```json
"_mapImage": {
  "_enabled": true,
  "_file": "BarbarossoBackground.png",
  "_position": [0.0, 11.0, 45.0],
  "_rotation": [0.0, 0.0, 0.0],
  "_scale": [3200.0, 2133.0, 1.0],
  "_showInMenu": false,
  "_showInMap": true
}
```

Имя файла не может содержать `/`, `\\` или `..`. Если блока нет, `_enabled` равен
`false` или файл отсутствует, изображение не создаётся.

## Установка

Установить `BARBAROSSOMOD_v0.1.qmod` через ModsBeforeFriday: `Add Mods` / `Upload Files`.
Зависимости `beatsaber-hook`, `BSML`, `custom-types` и `paper2_scotland2` перечислены
в `mod.json` пакета и должны быть установлены менеджером модов.

Старый глобальный Imager следует отключить, иначе он может одновременно показывать
свои глобальные placements.

## Происхождение

Quest-реализация основана на проекте Imager (`https://github.com/vcmikuu/imager`).
Автор Imager указан как первоначальный автор основы Quest-версии. BARBAROSSO получил
разрешение автора на публикацию этой производной версии; см. `../THIRD_PARTY_PERMISSION.md`.
