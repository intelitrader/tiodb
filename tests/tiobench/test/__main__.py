import os
import re
import subprocess
import tioclient
import unittest


class tio_bench_tests(unittest.TestCase):

    def setUp(self):
        self.tio_path = os.environ.get('TIO_PATH')
        if not self.tio_path:
            self.tio_path = 'tio.exe' if os.name == 'nt' else 'tio'
        self.tio_bench_path = os.environ.get('TIO_BENCH_PATH')
        if not self.tio_bench_path:
            self.tio_bench_path = 'tiobench.exe' if os.name == 'nt' else 'tiobench'
        self.total_operations = os.environ.get('TIO_BENCH_TOTAL_OPERATIONS') if os.environ.get('TIO_BENCH_TOTAL_OPERATIONS') else '1000'
        self.total_feeders = os.environ.get('TIO_BENCH_TOTAL_FEEDERS') if os.environ.get('TIO_BENCH_TOTAL_FEEDERS') else '20'
        self.total_clients = os.environ.get('TIO_BENCH_TOTAL_CLIENTS') if os.environ.get('TIO_BENCH_TOTAL_CLIENTS') else '3'
        self.operations_by_feeder = os.environ.get('TIO_BENCH_OPERATIONS_BY_FEEDER') if os.environ.get('TIO_BENCH_OPERATIONS_BY_FEEDER') else '100'

    def test_exes(self):
        print 'using tio_path:', self.tio_path
        self.assertTrue(os.path.isfile(self.tio_path) and os.access(self.tio_path, os.X_OK))
        print 'using tio_bench_path:', self.tio_bench_path
        self.assertTrue(os.path.isfile(self.tio_bench_path) and os.access(self.tio_bench_path, os.X_OK))

    def test_data_stress(self):
        if os.name != 'nt':
            print 'non Windows tests disabled'
            return
        tio_ps = subprocess.Popen([self.tio_path])
        tio_bench_ps = subprocess.Popen([self.tio_bench_path, 
            '--run-test-parallel-data-stress',
            '--umdf-feeder-test-total-operations', self.total_operations
            ], stdout=subprocess.PIPE)
        tio_bench_ret = tio_bench_ps.wait()
        tio_bench_out = tio_bench_ps.communicate()[0]
        print tio_bench_out
        self.assertTrue(tio_bench_ret == 0)
        tio_bench_out = tio_bench_out.replace('\r', '').replace('\n', '')
        m = re.match('.*n=([0-9]+).*MAP ([0-9a-z_]+).*', tio_bench_out)
        tio_cn = tioclient.connect('tio://localhost:2605')
        cont = tio_cn.open(m.group(2))
        self.assertTrue(len(cont) == int(m.group(1)))
        tio_ps.kill()

    def test_umdf_feeder_stress(self):
        if os.name != 'nt':
            print 'non Windows tests disabled'
            return
        tio_ps = subprocess.Popen([self.tio_path])
        tio_bench_args = [self.tio_bench_path,
            '--run-test-umdf-feeder-stress', 
            '--umdf-feeder-test-total-feeders', self.total_feeders,
            '--umdf-feeder-test-total-clients', self.total_clients,
            '--umdf-feeder-test-total-operations', self.operations_by_feeder
            ]
        print 'running ', ' '.join(tio_bench_args)
        tio_bench_ps = subprocess.Popen(tio_bench_args, stdout=subprocess.PIPE)
        tio_bench_ret = tio_bench_ps.wait()
        tio_bench_out = tio_bench_ps.communicate()[0]
        print tio_bench_out
        self.assertTrue(tio_bench_ret == 0)
        symbols = tio_bench_out[tio_bench_out.find('Symbols: ') + len('Symbols: '):]
        symbols = symbols[0:symbols.find('\r\n')]
        tio_cn = tioclient.connect('tio://localhost:2605')
        total_operations = 0
        for s in symbols.split(' '):
            containers = {
            'book_buy': tio_cn.open('intelimarket/bvmf/' + s + '/book_buy'),
            'book_sell': tio_cn.open('intelimarket/bvmf/' + s + '/book_sell'),
            'trades': tio_cn.open('intelimarket/bvmf/' + s + '/trades'),
            'properties': tio_cn.open('intelimarket/bvmf/' + s + '/properties')
            }
            total_operations = total_operations + len(containers['book_buy']) + len(containers['book_sell'])
        self.assertTrue(total_operations == int(self.operations_by_feeder) * int(self.total_feeders))
        tio_ps.kill()


if __name__ == '__main__':
    unittest.main()

