package WaterMonitor::Mailer;

# Email the .xlsx: the message (a few lines per site from the Summary) and
# SMTP with STARTTLS or SSL.

use strict;
use warnings;
use Email::MIME;
use File::Basename qw(basename);
use WaterMonitor::Daily qw(eastern);
use Exporter 'import';

our @EXPORT_OK = qw(body_text build_message send_message);

my $XLSX = 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet';

sub _cell { my $v = shift; return ref $v eq 'HASH' ? ($v->{str} // $v->{date}) : $v }

sub body_text {
    my ($summary, $now) = @_;
    my ($head, @rest) = @{ $summary->{rows} };
    my @lines = ('Water monitoring report, '
                 . eastern($now)->strftime('%Y-%m-%d %H:%M') . ' ET', '');
    for my $row (@rest) {
        last if @$row < @$head;
        my %r;
        @r{@$head} = map { _cell($_) } @$row;
        my $v = sub { my ($k, $u) = @_; defined $r{$k} ? "$r{$k}" . ($u // '') : 'n/a' };
        push @lines, "$r{Site} $r{Location}",
            '  last report ' . $v->('Last report (ET)') . ' ET (no. '
                . $v->('Report') . '), battery ' . $v->('Battery (%)', '%')
                . ', signal ' . $v->('Signal'),
            '  level last 7 days ' . $v->('Level low 7d (ft)') . ' to '
                . $v->('Level high 7d (ft)') . ' ft, change '
                . $v->('Level change 7d (ft)') . ' ft',
            '  missing reports: ' . $v->('Missing reports'), '';
    }
    push @lines, 'The attached water-report.xlsx holds every day and the trend charts.',
        'Times are US Eastern. Level is as the logger reports it, not corrected '
        . 'for air pressure.';
    return join("\n", @lines) . "\n";
}

sub build_message {
    my ($from, $to, $subject, $body, @files) = @_;
    my @parts = (Email::MIME->create(
        attributes => {content_type => 'text/plain', charset => 'UTF-8',
                       encoding => 'quoted-printable'},
        body_str => $body));
    for my $f (@files) {
        open my $fh, '<:raw', $f or die "cannot read $f: $!\n";
        my $data = do { local $/; <$fh> };
        push @parts, Email::MIME->create(
            attributes => {content_type => $XLSX, encoding => 'base64',
                           disposition => 'attachment', filename => basename($f),
                           name => basename($f)},
            body => $data);
    }
    return Email::MIME->create(
        header_str => [From => $from, To => join(', ', @$to), Subject => $subject],
        parts => \@parts);
}

# $e: the [email] settings. security: starttls (587), ssl (465) or none.
sub send_message {
    my ($msg, $e, $to) = @_;
    require Email::Sender::Simple;
    require Email::Sender::Transport::SMTP;
    my $sec = $e->{security} // 'starttls';
    my $transport = Email::Sender::Transport::SMTP->new(
        host => $e->{smtp_host}, port => $e->{smtp_port},
        ssl => ($sec eq 'ssl' ? 'ssl' : $sec eq 'starttls' ? 'starttls' : 0),
        ($e->{user} ? (sasl_username => $e->{user},
                       sasl_password => $ENV{ $e->{password_env} } // '') : ()),
        timeout => 60);
    Email::Sender::Simple->send($msg, {transport => $transport, to => $to});
}

1;
