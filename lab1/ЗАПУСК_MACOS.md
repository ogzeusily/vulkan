# Запуск Vulkan-проектов на macOS

## Уже настроено

- Настройки SDK автоматически загружаются в новых окнах zsh через `setup-env.sh`.
- Проверить доступность компилятора шейдеров: `glslc --version`.
- Стартовый проект находится в этой папке. Проект использует MoltenVK для запуска Vulkan поверх Metal.

## Сборка стартового приложения

Откройте Terminal и выполните:

```sh
cd "$HOME/бд/lr1"
cmake --preset debug
cmake --build build-debug --parallel
```

Запуск выполняйте из корня проекта, поскольку приложение читает ресурсы по относительным путям:

```sh
cd "$HOME/бд/lr1"
./build-debug/vulkan-starter-app
```

Если `glslc` не найден в уже открытом Terminal, запустите `source "$HOME/VulkanSDK/1.4.357.1/setup-env.sh"` либо откройте новое окно Terminal.