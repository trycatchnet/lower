<div align="center">
    <img height="160" alt="lower-logo" src="https://github.com/user-attachments/assets/8af845fe-0852-46d6-a8fd-f9b5a99ee9b1" />
</div>

<h1 align="center">Lower Web Framework</h1>

![Version](https://img.shields.io/badge/version-0.0.1-blue.svg)
![License](https://img.shields.io/github/license/trycatchh/lower?style=flat-square)

[📦 LowPM (Package Manager)](https://github.com/trycatchh/lowerpm) । [📚 Docs](https://github.com/trycatchh/lower/blob/main/README.md) । [👥 Community](https://discord.gg/mepa8X7j6w) । [🤝 Join Us](https://discord.gg/mepa8X7j6w)

- [What is Lower?](https://github.com/trycatchh/lower?tab=readme-ov-file#what-is-lower)
- [Getting Started](https://github.com/trycatchh/lower?tab=readme-ov-file#getting-started)
- [Integration](https://github.com/trycatchh/lower?tab=readme-ov-file#integration)
- [How to Use?](https://github.com/trycatchh/lower?tab=readme-ov-file#how-to-use)
- [How can I contribute?](https://github.com/trycatchh/lower?tab=readme-ov-file#how-can-i-contribute)
- [License (MIT)](https://github.com/trycatchh/lower?tab=readme-ov-file#license)

## What is Lower?
Lower Framework is a lightweight, modular web framework written in C that speeds up development with its flexibility and high performance. It allows you to customize and extend modules easily to fit your needs. With [LowPM](https://trycatch.network), integrating external libraries and managing modules becomes simple and efficient, making your projects faster and more maintainable.

## Getting Started
### Use LW Structure
[Copy the project](https://github.com/trycatchh/lower/archive/refs/heads/main.zip) directly and stand it up. All the structures that should be in the project will come, just commit your project on it.

### Include Your Project
Include basic modules for run
```shell
git clone https://github.com/trycatchh/lower.git
```
Add the library to your code file
```c
#include "lower/run.h" // Runtime module
```
To add libraries, please refer to the [documentation of the LowPM](https://trycatch.network/) repository.

## Integration
##### Create a handler
```c
void index_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    render_html(res, "index.html");
}
```
##### Register the handler and run the server
```c
int main(int argc, char *argv[]) {
    parameter_controller(argc, argv);
    use_static_files(); // if u want: (public: html/, css/, js/...)
    lw_route(GET, "/", index_handler);
    
    return lw_run(parameter_controller(argc, argv));
}
```
[Static Files Supports](https://github.com/trycatchh/lower/blob/fc2307e7e325985ee733444f018aa8c6f6b8fa34/html_handler.c#L159)

## How to Use?
If you have included LowerWF in your project later, compile it with your own build technique. But if you are using the project template, follow this path:
```shell
make clean && make # build project
```
You can then run the project. You can send commands to the binary file of the software as follows:
```shell
sudo ./build/lwserver -p 8282 -d
```
> With the -h argument, you can use the appropriate command from the command documentation and find out what the commands are.

## How can I contribute?
We welcome your contributions. If there is a software problem, do not hesitate to tell us and we will do our best.
We are always open to participation in our team. Please contact us on [Discord](https://discord.gg/mepa8X7j6w) or [Mail](mailto:p0unter@proton.me). We are always open to new ideas
- Create an [issue page](https://github.com/trycatchh/lower/issues) related to the problem.
- Request a suggestion: please submit your suggestion via the [issue page](https://github.com/trycatchh/lower/issues).
- Join us, [active team](https://github.com/trycatchh/lower/graphs/contributors).

## License
This project is licensed under the [MIT License](https://github.com/trycatchh/lower/blob/main/LICENSE), a permissive open-source license that lets you freely use, modify, and distribute the software. You must include the original copyright and license notice in any copies or substantial portions of the software.

The license provides the software "as is," without any warranty, protecting the authors from liability. Its simplicity and flexibility make it widely used and trusted in the open-source community.
