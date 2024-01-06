FROM debian:12
WORKDIR /src
RUN apt-get update && apt-get install build-essential -y
COPY . .
RUN make clean && make all

# debug images contain a busybox shell
FROM gcr.io/distroless/cc-debian12:debug
WORKDIR /home
COPY --from=0 /src/tcp_rr /bin/tcp_rr
COPY --from=0 /src/tcp_stream /bin/tcp_stream
COPY --from=0 /src/tcp_crr /bin/tcp_crr
COPY --from=0 /src/udp_rr /bin/udp_rr
COPY --from=0 /src/udp_stream /bin/udp_stream
COPY --from=0 /src/psp_stream /bin/psp_stream
COPY --from=0 /src/psp_crr /bin/psp_crr
COPY --from=0 /src/psp_rr /bin/psp_rr
# useful to deploy the image as a Kubernetes Pod
# as it keeps the image running forever
ENTRYPOINT [ "/busybox/sleep", "infinity" ]
