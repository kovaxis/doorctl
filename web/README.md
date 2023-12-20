# Doorctl-web

## Running

Build docker image:

```sh
docker build -t doorctl-web .
```

Run docker image:

```sh
docker run --rm --name doorctl -p 8888:80 doorctl-web
```

Build and run (useful command for development):

```sh
docker build -t doorctl-web . && docker run --rm --name doorctl -p 8888:80 doorctl-web
```
