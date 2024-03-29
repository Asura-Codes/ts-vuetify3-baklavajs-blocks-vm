<script setup lang="ts">
import Module from "../assets/vm";
import type { VM_t } from "../assets/vm";
import { compileFromString } from "../utils/compiler";
import svmasm from "../utils/svmasm";
import hljs from "highlight.js";
import CodeEditor from "simple-code-editor/CodeEditor.vue";
import VueCommand, {
  createStdout,
  textFormatter,
  jsonFormatter,
} from "vue-command";
import hex from "../utils/hex";
import "vue-command/dist/vue-command.css";

hljs.registerLanguage("svmasm", svmasm);
</script>

<template>
  <div>
    <v-item-group multiple>
      <v-btn
        class="ma-2"
        outlined
        x-large
        fab
        color="primary"
        @click="() => ($refs.console as any).dispatch('hex')"
      >
        Compile Program
      </v-btn>
      <v-btn
        class="ma-2"
        outlined
        x-large
        fab
        color="primary"
        @click="() => ($refs.console as any).dispatch('run')"
      >
        Run Program
      </v-btn>
      <!-- <v-btn
        class="ma-2"
        outlined
        x-large
        fab
        color="primary"
        @click="printBuffers"
      >
        Print Buffers
      </v-btn>
      <v-btn class="ma-2" outlined x-large fab color="primary" @click="debug">
        Debug
      </v-btn> -->
      <v-btn
        class="ma-2"
        outlined
        x-large
        fab
        color="primary"
        @click="() => ($refs.console as any).dispatch('help')"
      >
        Help
      </v-btn>
    </v-item-group>
    <div class="mx-auto" max-width="98vw" outlined>
      <v-data-table
        v-model:page="page"
        :headers="headers"
        :items="buffers"
        class="elevation-1"
        item-key="name"
        fixed-header
        :items-per-page="5"
        hide-default-footer
        height="238px"
      >
        <!-- <template v-slot:headers></template> -->
        <template v-slot:column.name="{ column }">
          {{ column.title.toUpperCase() }}
        </template>

        <template v-slot:body="{ items }">
          <tr v-for="(row, rowIdx) in items" :key="rowIdx">
            <td style="width: 120px">{{ row.columns.name }}</td>
            <td v-for="(item, idx) in row.raw.arr" :key="idx">
              {{ console.log(item) }}
              <v-text-field
                v-if="row.raw.editable"
                :value="item"
                @input="(event: any) => { row.raw.arr[idx] = row.raw.dest[idx] = Number(event.target.value); }"
                single-line
                class="num-field"
                hide-details
                type="number"
              ></v-text-field>
              <span v-else>{{ item }}</span>
            </td>
          </tr>
        </template>

        <template v-slot:bottom></template>
      </v-data-table>
    </div>
    <div id="editor">
      <v-row align="start" style="height: 100%">
        <v-col class="pa-0 ma-0" cols="4" style="height: 100%">
          <v-sheet class="pa-0 ma-0" style="height: 100%">
            <CodeEditor
              v-model="code"
              :line-nums="true"
              :languages="[['svmasm', 'SVM']]"
              :tab-spaces="4"
              :wrap="false"
              :header="true"
              :display-language="true"
              theme="github-dark-dimmed"
              font-size="22px"
              width="100%"
              height="100%"
              padding="8px"
              border-radius="4px"
              :copy-code="true"
              :lang-list-display="false"
              color="white"
            ></CodeEditor>
          </v-sheet>
        </v-col>
        <v-col class="pa-0 ma-0" style="height: 100%">
          <v-sheet class="pa-0 ma-0" style="height: 100%">
            <vue-command
              ref="console"
              :commands="commands"
              hide-bar
              style="height: 100%; width: 96%"
            />
          </v-sheet>
        </v-col>
      </v-row>
    </div>
  </div>
</template>

<style scoped>
#editor {
  display: block;
  height: calc(100vh - 330px);
  line-height: normal !important;
}
.num-field {
  margin-left: 0px !important;
  margin-right: 0px !important;
}
</style>

<script lang="ts">
export default {
  data: () => ({
    vm: undefined as VM_t | undefined,
    arrAnalogInputs: undefined as Float32Array | undefined,
    arrAnalogOuputs: undefined as Float32Array | undefined,
    arrBinaryInputs: undefined as Uint8Array | undefined,
    arrBinaryOuputs: undefined as Uint8Array | undefined,
    arrVariables: undefined as Array<number> | undefined,
    code: "" as string,
    program: undefined as Uint8Array | undefined,

    // Table
    page: 1,
    buffers: [] as any[],
    msg: "",
  }),
  props: {
    codeText: {
      type: String,
    },
  },
  methods: {
    async compileProgram() {
      this.program = await compileFromString(this.code);

      console.log("program:");
      console.log(this.program);
    },
    async runProgram() {
      await this.compileProgram();
      this.setBuffers();

      if (this.program) {
        this.vm?.RunProgram(this.program);
      }

      this.getBuffers();
      this.fillBuffersTable();
    },
    getBuffers() {
      this.arrAnalogInputs = this.vm?.getAnalogInputs();
      this.arrAnalogOuputs = this.vm?.getAnalogOuputs();
      this.arrBinaryInputs = this.vm?.getBinaryInputs();
      this.arrBinaryOuputs = this.vm?.getBinaryOuputs();
      this.arrVariables = this.vm?.getVariables();
    },
    setBuffers() {
      if (this.arrAnalogInputs) this.vm?.setAnalogInputs(this.arrAnalogInputs);
      if (this.arrBinaryInputs) this.vm?.setBinaryInputs(this.arrBinaryInputs);
      if (this.arrVariables) this.vm?.setVariables(this.arrVariables);
    },
    printBuffers() {
      this.vm?.printAnalogInputs();
      this.vm?.printAnalogOuputs();
      this.vm?.printBinaryInputs();
      this.vm?.printBinaryOuputs();
      this.vm?.printVariables();
    },
    debug() {
      console.log(this.arrAnalogInputs);
      console.log(this.arrAnalogOuputs);
      console.log(this.arrBinaryInputs);
      console.log(this.arrBinaryOuputs);
      console.log(this.arrVariables);
    },
    fillBuffersTable() {
      if (
        this.arrAnalogInputs &&
        this.arrAnalogOuputs &&
        this.arrBinaryInputs &&
        this.arrBinaryOuputs &&
        this.arrVariables
      ) {
        this.buffers = [
          {
            name: "Analog IN",
            editable: true,
            arr: Array.from(this.arrAnalogInputs),
            dest: this.arrAnalogInputs,
          },
          {
            name: "Binary IN",
            editable: true,
            arr: Array.from(this.arrBinaryInputs),
            dest: this.arrBinaryInputs,
          },
          {
            name: "VARS",
            editable: true,
            arr: this.arrVariables,
            dest: this.arrVariables,
          },
          {
            name: "Analog Out",
            arr: Array.from(this.arrAnalogOuputs),
            dest: this.arrAnalogOuputs,
          },
          {
            name: "Binary OUT",
            arr: Array.from(this.arrBinaryOuputs),
            dest: this.arrBinaryOuputs,
          },
        ];
      }
    },
  },
  computed: {
    headers() {
      const hds = [
        { title: "Buf. name", align: "start", key: "name" },
        ...Array.from(
          { length: this.arrAnalogInputs?.length ?? 0 },
          (_, i) => ({ title: `[${i}]`, align: "start", key: `${i}` })
        ),
      ];
      return hds as any;
    },

    commands() {
      return {
        run: async () => {
          this.msg = "";
          await this.runProgram();
          return createStdout(this.msg);
        },
        hex: async () => {
          await this.compileProgram();
          if (this.program) {
            let printed = hex(this.program);
            return createStdout(textFormatter(printed, true));
          }
        },
        bufs: async () => {
          await this.getBuffers();
          const obj = {
            AnalogInputs: this.arrAnalogInputs,
            AnalogOuputs: this.arrAnalogOuputs,
            BinaryInputs: this.arrBinaryInputs,
            BinaryOuputs: this.arrBinaryOuputs,
            Variables: this.arrVariables,
          };
          return createStdout(textFormatter(JSON.stringify(obj, null, 4).replaceAll(' ', '&nbsp;'), true));
        },
        help: async () => {
          const helpStr =
            '<span style="color: DodgerBlue; ">Commands:</span>\n' +
            '&nbsp;&nbsp;&nbsp;&nbsp;<span style="color: MediumSeaGreen; ">help</span> - This help\n' +
            '&nbsp;&nbsp;&nbsp;&nbsp;<span style="color: MediumSeaGreen; ">run</span> &nbsp;- Execute program (once)\n' +
            '&nbsp;&nbsp;&nbsp;&nbsp;<span style="color: MediumSeaGreen; ">hex</span> &nbsp;- Compiled binary (hex formatted)\n' +
            '&nbsp;&nbsp;&nbsp;&nbsp;<span style="color: MediumSeaGreen; ">bufs</span> - Print buffers (json formatted)\n\n';
          return createStdout(textFormatter(helpStr, true));
        },
      };
    },
  },
  mounted() {
    if (this.codeText) this.code = this.codeText;
    console.log("RuntimeView mounted");

    if (this.vm == undefined) {
      Module().then((myModule: VM_t) => {
        this.vm = myModule;
        this.getBuffers();
        this.fillBuffersTable();
      });
    }

    (window as any).createStdoutQ8YQPV9U = (msg: string) => {
      this.msg += msg;
    };
  },
  beforeUnmount() {
    console.log("RuntimeView beforeUnmount");
  },
  unmounted() {
    console.log("RuntimeView unmounted");
  },
  components: {
    CodeEditor,
  },
};
</script>
