FROM rust:1-alpine AS build
RUN apk add --no-cache musl-dev
WORKDIR /src
COPY . .
RUN cargo build --release --locked

FROM scratch
COPY --from=build /src/target/release/worms4nx-server /worms4nx-server
EXPOSE 7777
ENTRYPOINT ["/worms4nx-server"]
