# RouteHacker API: Express server + compiled C++ route planner.
# Used by Render (see render.yaml). Build locally with:
#   docker build -t routehacker-api . && docker run -p 3000:3000 routehacker-api

FROM debian:bookworm-slim AS planner
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make pkg-config libexpat1-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /build
COPY Makefile ./
COPY include ./include
COPY src ./src
RUN make directories && make bin/routeplanner_web

FROM node:22-bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends libexpat1 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY package.json package-lock.json ./
COPY server/package.json ./server/
COPY client/package.json ./client/
RUN npm ci --omit=dev --ignore-scripts --workspace server --include-workspace-root=false
COPY server ./server
COPY shared ./shared
COPY data ./data
COPY --from=planner /build/bin/routeplanner_web ./bin/routeplanner_web

ENV NODE_ENV=production \
    PORT=3000 \
    ROUTE_ENGINE=cpp
EXPOSE 3000
USER node
CMD ["node", "server/src/index.js"]
