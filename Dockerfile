FROM zmkfirmware/zmk-build-arm:stable

# Everything repeatable happens at image build time, so `docker compose run build` is
# just a compile. config/west.yml pins zmk to the moving branch `halcyon-split-status`;
# bump REFRESH to bust the layer cache when that branch has moved but the file has not:
#   docker compose build --build-arg REFRESH=$(date +%s)
ARG REFRESH=0

COPY config/west.yml /west/config/west.yml
WORKDIR /west
RUN west init -l /west/config \
 && west update --fetch-opt=--filter=tree:0 \
 && west zephyr-export

COPY docker/build-firmware.sh /usr/local/bin/build-firmware
RUN chmod +x /usr/local/bin/build-firmware

ENTRYPOINT ["/usr/local/bin/build-firmware"]
