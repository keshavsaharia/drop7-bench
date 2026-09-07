# AWS Lambda container image for the search-decision pilot handler.
#
# Built FROM the same public.ecr.aws/lambda/provided:al2023 base in both
# stages so the compiled binary's glibc matches the runtime's exactly; a
# binary built on this workstation (Debian, a newer glibc) or in a generic
# Rust image is not guaranteed to run on AL2023's older glibc symbol
# versions. Deliberately does NOT use -C target-cpu=native (see
# infra/lambda-teacher/Cargo.toml): this binary runs on whatever CPU AWS
# schedules the function on, which this workstation does not control.
#
# Build context is the REPOSITORY ROOT (the crate's Cargo.toml has a path
# dependency on approaches/fair-expectimax/rust-engine, outside this
# directory):
#   podman build -f infra/lambda-teacher/Dockerfile -t drop7-lambda-teacher:pilot .

FROM public.ecr.aws/lambda/provided:al2023 AS builder
RUN dnf install -y gcc gcc-c++ make tar gzip which \
    && dnf clean all
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:$PATH
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --profile minimal --default-toolchain stable
WORKDIR /build
COPY approaches/fair-expectimax/rust-engine approaches/fair-expectimax/rust-engine
COPY infra/lambda-teacher infra/lambda-teacher
WORKDIR /build/infra/lambda-teacher
RUN cargo build --release --locked || cargo build --release
RUN cp target/release/bootstrap /tmp/bootstrap

FROM public.ecr.aws/lambda/provided:al2023
COPY --from=builder /tmp/bootstrap ${LAMBDA_TASK_ROOT}/bootstrap
RUN chmod +x ${LAMBDA_TASK_ROOT}/bootstrap
CMD ["bootstrap"]
